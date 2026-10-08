// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_TEXT_GLYPH_ATLAS_H
#define ELG_TEXT_GLYPH_ATLAS_H

#include "text/atlas.h"
#include "text/font_library.h"

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace elg::detail {

// Where a glyph is in the atlas and how its quad sits relative to the pen position.
struct AtlasGlyph {
    // Normalized texture coordinates, top-left to bottom-right.
    float u0 = 0, v0 = 0, u1 = 0, v1 = 0;
    // Quad edges in em units relative to the pen position on the baseline, y up.
    float left = 0, bottom = 0, right = 0, top = 0;
};

// The UI-side glyph atlas: generates MTSDF glyphs on first use and queues their pixels for the
// render thread, which owns the texture (design.md §15). UI thread only.
// TODO: generate on the worker pool; evict (LRU) or add pages when full.
class GlyphAtlas {
public:
    // nullptr for glyphs with no outline (spaces), or when the atlas is full.
    const AtlasGlyph* find_or_add(const FontFace& face, std::uint32_t glyph_id);

    // New glyph pixels since the last call; post them before any frame that uses them.
    std::vector<GlyphUpload> take_uploads();

private:
    std::optional<AtlasGlyph> generate(const FontFace& face, std::uint32_t glyph_id);
    bool allocate(std::uint32_t width, std::uint32_t height, std::uint32_t& x, std::uint32_t& y);

    std::unordered_map<std::uint64_t, std::optional<AtlasGlyph>> glyphs_;
    std::vector<GlyphUpload> uploads_;
    std::uint32_t shelf_x_ = 0;
    std::uint32_t shelf_y_ = 0;
    std::uint32_t shelf_height_ = 0;
    bool reported_full_ = false;
};

} // namespace elg::detail

#endif // ELG_TEXT_GLYPH_ATLAS_H
