// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_TEXT_GLYPH_CACHE_H
#define ELG_TEXT_GLYPH_CACHE_H

#include "text/font_library.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

namespace elg::detail {

// What a glyph's distance field holds.
enum class DistanceField : std::uint8_t {
    multi_channel,  // MTSDF: text
    single_channel, // one distance in every channel: icons and color glyphs
};

// Generated glyph pixels kept on disk between runs (design.md §11), so a warm start uploads
// glyphs instead of generating them. One file per font and kind of distance field, named by a
// hash of the font's bytes and of its variation coordinates (an updated font gets a new file);
// the file's header holds the glyph size and distance range. Records are appended as glyphs are
// generated, each with a checksum, so a torn or interleaved write only loses the records after
// it. Thread-safe.
class GlyphDiskCache {
public:
    // `directory`: where the files go (created if needed); nullopt disables the cache.
    explicit GlyphDiskCache(std::optional<std::filesystem::path> directory);
    ~GlyphDiskCache();

    GlyphDiskCache(const GlyphDiskCache&) = delete;
    GlyphDiskCache& operator=(const GlyphDiskCache&) = delete;
    GlyphDiskCache(GlyphDiskCache&&) = delete;
    GlyphDiskCache& operator=(GlyphDiskCache&&) = delete;

    [[nodiscard]] bool enabled() const noexcept { return directory_.has_value(); }

    // The cached pixels (RGBA) of `glyph` as a `field` distance field at `width` x `height`, if
    // any.
    std::optional<std::vector<std::uint8_t>> find(const FontFace& face, DistanceField field, std::uint32_t glyph,
                                                  std::uint32_t width, std::uint32_t height);
    void store(const FontFace& face, DistanceField field, std::uint32_t glyph, std::uint32_t width,
               std::uint32_t height, std::span<const std::uint8_t> rgba);

    // Where the cache lives by default: the platform's cache folder, or ELG_GLYPH_CACHE (a path,
    // or "off").
    static std::optional<std::filesystem::path> default_directory();

private:
    struct Record {
        std::uint32_t width;
        std::uint32_t height;
        std::size_t offset; // into the loaded file
    };
    struct FontCache {
        std::filesystem::path file;
        std::vector<char> contents; // as loaded
        std::unordered_map<std::uint32_t, Record> records;
        std::ofstream out;
        std::uint64_t bytes = 0;
    };

    FontCache& font(const FontFace& face, DistanceField field); // with the mutex held

    std::optional<std::filesystem::path> directory_;
    std::mutex mutex_;
    std::unordered_map<std::uint64_t, std::unique_ptr<FontCache>> fonts_; // by face id and field
};

} // namespace elg::detail

#endif // ELG_TEXT_GLYPH_CACHE_H
