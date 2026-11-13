// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// The glyph atlas (one page of distance-field glyphs, placed as frames draw them), and the
// system fonts its glyphs come from.

#include "text/font_library.h"
#include "text/glyph_atlas.h"
#include "text/text_system.h"

#include <elg/elegance.h>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>

using namespace elg;

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

    // After the frame (its uploads taken), the atlas starts over and asks for a new frame.
    atlas.take_uploads();
    CHECK(atlas.has_uploads());
    CHECK(atlas.find_or_add(face, *probe) != nullptr);
    // NOLINTEND(bugprone-unchecked-optional-access)
}

#if defined(__linux__)
TEST_CASE("the system font for each weight is a file that exists", "[text][linux]") {
    const auto regular = detail::system_font_candidates(Weight::regular, false);
    const auto bold = detail::system_font_candidates(Weight::bold, false);
    REQUIRE_FALSE(regular.empty());
    REQUIRE_FALSE(bold.empty());
    CHECK(std::filesystem::is_regular_file(regular.front()));
    CHECK(std::filesystem::is_regular_file(bold.front()));
    CHECK(regular.front() != bold.front());
}
#endif
