// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_TEXT_GLYPH_DRAW_H
#define ELG_TEXT_GLYPH_DRAW_H

#include "render/display_list.h"
#include "text/glyph_atlas.h"
#include "text/text_layout.h"

namespace elg::detail {

// Draws one shaped glyph with its pen at (pen, baseline), `size` points per em. Color glyphs
// (COLR emoji) draw as their layers, bottom to top, each in its palette color; a layer marked
// "foreground", and every other glyph, takes `color`.
inline void draw_glyph(DisplayList& list, GlyphAtlas& atlas, const FontFace& face, const ShapedGlyph& glyph, float pen,
                       float baseline, float size, Color color) {
    const auto emit = [&](std::uint32_t id, Color tint) {
        if (const AtlasGlyph* placed = atlas.find_or_add(face, id)) {
            const Rect quad{pen + (glyph.x_offset + placed->left) * size, baseline - (glyph.y_offset + placed->top) * size,
                            (placed->right - placed->left) * size, (placed->top - placed->bottom) * size};
            const Rect uv{placed->u0, placed->v0, placed->u1 - placed->u0, placed->v1 - placed->v0};
            list.commands.push_back(GlyphCommand{quad, uv, tint, size});
        }
    };
    const std::span<const ColorLayer> layers = face.color_layers(glyph.id);
    if (layers.empty()) {
        emit(glyph.id, color);
        return;
    }
    for (const ColorLayer& layer : layers) {
        Color tint = layer.foreground ? color : layer.color;
        if (!layer.foreground) {
            tint.a *= color.a; // fades with the text
        }
        emit(layer.glyph, tint);
    }
}

} // namespace elg::detail

#endif // ELG_TEXT_GLYPH_DRAW_H
