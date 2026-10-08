// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_TEXT_ATLAS_H
#define ELG_TEXT_ATLAS_H

// Shared between the UI-side glyph atlas (text/glyph_atlas.h) and the GPU texture it fills
// (gpu/atlas_texture.h).

#include <cstdint>
#include <vector>

namespace elg::detail {

// Glyph atlas: one RGBA8 page of MTSDF glyphs (design.md §5 Text rendering).
inline constexpr std::uint32_t kAtlasSize = 2048;
// Glyphs are generated at this em size; the distance field scales to any size from there.
inline constexpr float kGlyphEmPixels = 48;
// Width, in atlas pixels, of the distance field around each edge. Wider allows bigger
// outline/glow effects; narrower is crisper at small sizes.
inline constexpr float kDistanceRange = 6;

// A rectangle of new glyph pixels for the render thread to copy into the atlas texture.
struct GlyphUpload {
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> rgba; // width * height * 4, top row first
};

} // namespace elg::detail

#endif // ELG_TEXT_ATLAS_H
