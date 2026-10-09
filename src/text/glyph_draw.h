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
    // COLRv1: filled regions with gradients, drawn as vector paths.
    if (const std::span<const ColorPath> paths = face.color_paths(glyph.id); !paths.empty()) {
        const Point origin{pen + glyph.x_offset * size, baseline - glyph.y_offset * size};
        const auto tint = [&](Color c, bool foreground) {
            return foreground ? color.opacity(c.a) : c.opacity(color.a);
        };
        for (const ColorPath& path : paths) {
            const Rect frame{origin.x + path.bounds.x * size, origin.y + path.bounds.y * size, path.bounds.width * size,
                             path.bounds.height * size};
            const auto unit = [&](Point p) {
                return UnitPoint{(p.x - path.bounds.x) / path.bounds.width, (p.y - path.bounds.y) / path.bounds.height};
            };
            const ColorPaint& paint = path.paint;
            std::vector<GradientStop> stops;
            for (const ColorStop& stop : paint.stops) {
                stops.push_back({tint(stop.color, stop.foreground), stop.offset});
            }
            ShapeStyle fill(Color::clear);
            switch (paint.kind) {
            case ColorPaint::Kind::solid: fill = ShapeStyle(tint(paint.color, paint.foreground)); break;
            case ColorPaint::Kind::linear: fill = ShapeStyle(LinearGradient(std::move(stops), unit(paint.p0), unit(paint.p1))); break;
            case ColorPaint::Kind::radial:
                fill = ShapeStyle(RadialGradient(std::move(stops), unit(paint.p0), paint.r0 * size, paint.r1 * size));
                break;
            }
            list.commands.push_back(PathCommand{path.polygons, path.bounds, size, origin, frame, std::move(fill),
                                                FillRule::non_zero});
        }
        return;
    }
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
