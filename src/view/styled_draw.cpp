// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include "view/styled_draw.h"

#include "view/glyph_draw.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace elg::detail {

namespace {

constexpr float kBackgroundPad = 2; // points each side of a highlighted run
constexpr float kBackgroundRadius = 3;

} // namespace

void draw_styled(DisplayList& list, GlyphAtlas& atlas, const FontFace& base, const StyledShape& shape,
                 const TextLayoutResult& layout, const std::vector<DrawStyle>& styles, Point origin, float size,
                 float scale, const std::vector<float>& line_x) {
    const auto snap = [&](float value) { return std::round(value * scale) / scale; };
    const auto style_of = [&](std::uint32_t index) -> const DrawStyle& {
        return styles[std::min<std::size_t>(index, styles.size() - 1)];
    };
    for (std::size_t li = 0; li < layout.lines.size(); ++li) {
        const TextLine& line = layout.lines[li];
        if (line.paragraph >= shape.paragraphs.size() || line.end > shape.paragraphs[line.paragraph].glyphs.size()) {
            break; // lines from older text: nothing sensible to draw
        }
        const ShapedParagraph& paragraph = shape.paragraphs[line.paragraph];
        const std::vector<std::uint32_t>& glyph_styles = shape.styles[line.paragraph];
        const float left = origin.x + (li < line_x.size() ? line_x[li] : 0.f);
        const float top = origin.y + static_cast<float>(li) * shape.line_height;
        const float baseline = snap(top + shape.ascent);

        // Backgrounds and lines first, per stretch of one style, under the glyphs.
        float pen = left;
        std::size_t g = line.begin;
        while (g < line.end) {
            const std::uint32_t style = glyph_styles[g];
            const float start = pen;
            while (g < line.end && glyph_styles[g] == style) {
                pen += paragraph.glyphs[g].advance * size;
                ++g;
            }
            const DrawStyle& look = style_of(style);
            if (look.background) {
                list.commands.emplace_back(RectCommand{{start - kBackgroundPad, top + 1, pen - start + 2 * kBackgroundPad,
                                                     shape.line_height - 2},
                                                    CornerRadii::all(kBackgroundRadius), ShapeStyle(*look.background),
                                                    Color::clear, 0});
            }
            if (look.underline) {
                list.commands.emplace_back(RectCommand{{start, snap(baseline + 2), pen - start, 1}, {}, ShapeStyle(look.color), Color::clear, 0});
            }
            if (look.strikethrough) {
                list.commands.emplace_back(RectCommand{{start, snap(baseline - shape.ascent * 0.3f), pen - start, 1}, {},
                                                    ShapeStyle(look.color), Color::clear, 0});
            }
        }

        pen = left;
        for (std::size_t i = line.begin; i < line.end; ++i) {
            const ShapedGlyph& glyph = paragraph.glyphs[i];
            if (glyph.placeholder) {
                pen += glyph.advance * size;
                continue;
            }
            draw_glyph(list, atlas, glyph.face ? *glyph.face : base, glyph, pen, baseline, size,
                       style_of(glyph_styles[i]).color);
            pen += glyph.advance * size;
        }
    }
}

} // namespace elg::detail
