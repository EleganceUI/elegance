// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// The glyph atlas (one page of distance-field glyphs, placed as frames draw them), and the
// system fonts its glyphs come from.

#include "text/font_library.h"
#include "text/glyph_atlas.h"
#include "text/text_backend.h"
#include "text/text_system.h"

#include <elg/elegance.h>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <system_error>
#include <vector>

using namespace elg;
using namespace std::chrono_literals;

namespace {

// A fresh directory for a disk cache, removed afterwards.
struct TemporaryDirectory {
    std::filesystem::path path;
    TemporaryDirectory() {
        std::random_device random;
        path = std::filesystem::temp_directory_path() / std::format("elg-glyph-test-{:x}", random());
        std::filesystem::create_directories(path);
    }
    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory(TemporaryDirectory&&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(TemporaryDirectory&&) = delete;
    ~TemporaryDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
};

// The first glyph of `face` with an outline, and its pixels as generated (no disk cache).
detail::GlyphUpload first_glyph(const detail::FontFace& face, std::uint32_t& glyph) {
    detail::GlyphAtlas atlas;
    for (glyph = 1; glyph < 1000 && !atlas.find_or_add(face, glyph); ++glyph) {
    }
    REQUIRE(atlas.wait_idle(10s));
    std::vector<detail::GlyphUpload> uploads = atlas.take_uploads();
    REQUIRE(uploads.size() == 1);
    return std::move(uploads[0]);
}

// The pixels `atlas` uploads for `glyph`, its first.
std::vector<std::uint8_t> uploaded(detail::GlyphAtlas& atlas, const detail::FontFace& face, std::uint32_t glyph) {
    REQUIRE(atlas.find_or_add(face, glyph) != nullptr);
    REQUIRE(atlas.wait_idle(10s));
    std::vector<detail::GlyphUpload> uploads = atlas.take_uploads();
    REQUIRE(uploads.size() == 1);
    return std::move(uploads[0].rgba);
}

// The file the system font comes from.
std::filesystem::path system_font_file() {
    std::filesystem::path found;
    for (const std::filesystem::path& file : detail::system_font_candidates(static_cast<int>(Weight::regular), false)) {
        if (std::filesystem::is_regular_file(file)) {
            found = file;
            break;
        }
    }
    REQUIRE_FALSE(found.empty()); // no system font
    return found;
}

} // namespace

TEST_CASE("the disk cache's files are read by the glyph workers, not on the UI thread", "[text]") {
    detail::TextSystem text;
    const detail::FontFace& face = text.fonts.resolve(Font::system(12));
    std::uint32_t glyph = 0;
    const detail::GlyphUpload generated = first_glyph(face, glyph);

    // Pixels no generator makes, stored as an earlier run would have.
    const TemporaryDirectory directory;
    const std::vector<std::uint8_t> marked(generated.rgba.size(), 7);
    {
        detail::GlyphDiskCache disk(directory.path);
        disk.store(face, detail::DistanceField::multi_channel, glyph, generated.width, generated.height, marked);
    }

    detail::GlyphAtlas atlas;
    atlas.set_disk_cache(directory.path);
    REQUIRE(atlas.find_or_add(face, glyph) != nullptr);
    CHECK(atlas.requested() == 1); // a worker's job, though the disk cache has it
    REQUIRE(atlas.wait_idle(10s));
    const std::vector<detail::GlyphUpload> uploads = atlas.take_uploads();
    REQUIRE(uploads.size() == 1);
    CHECK(uploads[0].rgba == marked);
}

TEST_CASE("a full glyph atlas starts over after the frame instead of dropping glyphs for good", "[text]") {
    detail::TextSystem text;
    const detail::FontFace& face = text.fonts.resolve(Font::system(12));
    // About twice what a page holds; the system fonts Elegance finds have more glyphs than this.
    constexpr std::uint32_t glyphs = 2500;

    // A glyph with an outline, found in an atlas of its own.
    std::optional<std::uint32_t> probe;
    {
        detail::GlyphAtlas scout;
        for (std::uint32_t id = 1; id < glyphs && !probe; ++id) {
            if (scout.find_or_add(face, id)) {
                probe = id;
            }
        }
    }
    REQUIRE(probe);
    // NOLINTBEGIN(bugprone-unchecked-optional-access): REQUIRE(probe) stops the test without one

    // Every other glyph of the font, far more than a page holds: then there's no room for it.
    detail::GlyphAtlas atlas;
    for (std::uint32_t id = 1; id < glyphs; ++id) {
        if (id != *probe) {
            atlas.find_or_add(face, id);
        }
    }
    REQUIRE(atlas.find_or_add(face, *probe) == nullptr);
    CHECK(atlas.layout() == 0);

    // After the frame (its uploads taken), the atlas starts over and asks for a new frame. The
    // uploads from then on are for a new layout, which replaces the old one's glyphs.
    atlas.take_uploads();
    CHECK(atlas.layout() == 1);
    CHECK(atlas.has_uploads());
    CHECK(atlas.find_or_add(face, *probe) != nullptr);
    // NOLINTEND(bugprone-unchecked-optional-access)
}

TEST_CASE("a glyph from the disk cache has the kind of distance field asked for", "[text]") {
    // One font file, as an icon font (single-channel fields) and as a text font (multi-channel).
    const std::filesystem::path file = system_font_file();
    register_font("elg.test.fields.text", file);
    detail::mark_icon_font_family("elg.test.fields.icon");
    register_font("elg.test.fields.icon", file);
    detail::FontLibrary fonts;
    const detail::FontFace& text = fonts.resolve(Font::custom("elg.test.fields.text", 12));
    const detail::FontFace& icon = fonts.resolve(Font::custom("elg.test.fields.icon", 12));
    std::uint32_t glyph = 0;
    const std::vector<std::uint8_t> generated = first_glyph(text, glyph).rgba;

    const TemporaryDirectory directory;
    {
        detail::GlyphAtlas atlas; // stores the icon's glyph
        atlas.set_disk_cache(directory.path);
        REQUIRE(uploaded(atlas, icon, glyph) != generated);
    }
    detail::GlyphAtlas atlas;
    atlas.set_disk_cache(directory.path);
    CHECK(uploaded(atlas, text, glyph) == generated);
}

TEST_CASE("a font file registered for icons and for text is drawn as each use was registered", "[text]") {
    const std::filesystem::path file = system_font_file();
    register_font("elg.test.roles.text", file);
    detail::mark_icon_font_family("elg.test.roles.icon");
    register_font("elg.test.roles.icon", file);
    for (const bool icon_first : {true, false}) {
        detail::FontLibrary fonts;
        const char* const order[] = {"elg.test.roles.text", "elg.test.roles.icon"};
        const detail::FontFace& first = fonts.resolve(Font::custom(order[icon_first ? 1 : 0], 12));
        const detail::FontFace& second = fonts.resolve(Font::custom(order[icon_first ? 0 : 1], 12));
        CHECK(first.icon() == icon_first);
        CHECK(second.icon() == !icon_first);
    }
}

TEST_CASE("faces with the same glyphs share one disk cache file", "[text]") {
    // One font's bytes, registered from memory as two families: two faces, one cache file.
    const std::filesystem::path file = system_font_file();
    std::ifstream in(file, std::ios::binary);
    const std::vector<unsigned char> bytes{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    register_font("elg.test.shared.a", std::span(bytes));
    register_font("elg.test.shared.b", std::span(bytes));
    detail::FontLibrary fonts;
    const detail::FontFace& a = fonts.resolve(Font::custom("elg.test.shared.a", 12));
    const detail::FontFace& b = fonts.resolve(Font::custom("elg.test.shared.b", 12));
    REQUIRE(&a != &b);

    const TemporaryDirectory directory;
    constexpr auto kField = detail::DistanceField::multi_channel;
    const std::vector<std::uint8_t> one(std::size_t{2} * 2 * 4, 1);
    const std::vector<std::uint8_t> two(std::size_t{2} * 3 * 4, 2);
    {
        detail::GlyphDiskCache disk(directory.path);
        // Both open the file before either stores a glyph.
        CHECK_FALSE(disk.find(a, kField, 1, 2, 2));
        CHECK_FALSE(disk.find(b, kField, 2, 2, 3));
        disk.store(a, kField, 1, 2, 2, one);
        disk.store(b, kField, 2, 2, 3, two);
    }
    detail::GlyphDiskCache disk(directory.path);
    CHECK(disk.find(a, kField, 1, 2, 2) == one);
    CHECK(disk.find(b, kField, 2, 2, 3) == two);
    CHECK(disk.find(a, kField, 2, 2, 3) == two); // the same glyphs
}

TEST_CASE("writing a font's cache file deletes its files of the earlier naming", "[text]") {
    detail::TextSystem text;
    const detail::FontFace& face = text.fonts.resolve(Font::system(12));
    std::uint32_t glyph = 0;
    const detail::GlyphUpload generated = first_glyph(face, glyph);
    const TemporaryDirectory directory;
    const auto store = [&] {
        detail::GlyphDiskCache disk(directory.path);
        disk.store(face, detail::DistanceField::multi_channel, glyph, generated.width, generated.height,
                   generated.rgba);
    };
    store();
    // The font's file: its name starts with the hash of the font's bytes.
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator(directory.path)) {
        files.push_back(entry.path());
    }
    REQUIRE(files.size() == 1);
    const std::string name = files[0].filename().string();
    const std::string hash = name.substr(0, name.find('-'));
    REQUIRE(hash.size() == 16);

    // Written again, after files named as before turned up for it and for another font.
    std::filesystem::remove(files[0]);
    const std::string other = std::string(16, '0') == hash ? std::string(16, '1') : std::string(16, '0');
    for (const std::string& old : {hash + ".glyphs", hash + "-wght400.glyphs", hash + "-wght700.glyphs",
                                   other + ".glyphs", other + "-wght400.glyphs"}) {
        std::ofstream(directory.path / old) << "old";
    }
    store();
    CHECK(std::filesystem::exists(directory.path / name));
    CHECK_FALSE(std::filesystem::exists(directory.path / (hash + ".glyphs")));
    CHECK_FALSE(std::filesystem::exists(directory.path / (hash + "-wght400.glyphs")));
    CHECK_FALSE(std::filesystem::exists(directory.path / (hash + "-wght700.glyphs")));
    CHECK(std::filesystem::exists(directory.path / (other + ".glyphs")));
    CHECK(std::filesystem::exists(directory.path / (other + "-wght400.glyphs")));
}

#if defined(__linux__)
TEST_CASE("the system font for each weight is a file that exists", "[text][linux]") {
    const auto regular = detail::system_font_candidates(static_cast<int>(Weight::regular), false);
    const auto bold = detail::system_font_candidates(static_cast<int>(Weight::bold), false);
    REQUIRE_FALSE(regular.empty());
    REQUIRE_FALSE(bold.empty());
    CHECK(std::filesystem::is_regular_file(regular.front()));
    CHECK(std::filesystem::is_regular_file(bold.front()));
    CHECK(regular.front() != bold.front());
}
#endif
