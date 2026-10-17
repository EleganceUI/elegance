// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include "text/styled_layout.h"

#include "text/glyph_draw.h"

#include <algorithm>
#include <cmath>

namespace elg::detail {

namespace {

constexpr float kBackgroundPad = 2;    // points each side of a highlighted run
constexpr float kBackgroundRadius = 3;

} // namespace

StyledShape shape_styled(std::string_view text, const std::vector<StyledRunSpec>& runs, const FontFace& base,
                         float size, FontLibrary& fonts) {
    StyledShape out;
    const auto measure_face = [&](const FontFace& face) {
        out.ascent = std::max(out.ascent, face.ascender() * size);
        out.line_height = std::max(out.line_height, (face.ascender() - face.descender() + face.line_gap()) * size);
    };
    measure_face(base);
    for (const StyledRunSpec& run : runs) {
        if (run.face) {
            measure_face(*run.face);
        }
    }

    std::size_t paragraph_start = 0;
    while (true) {
        const std::size_t newline = text.find('\n', paragraph_start);
        const std::size_t paragraph_end = newline == std::string_view::npos ? text.size() : newline;
        ShapedParagraph paragraph;
        std::vector<std::uint32_t> styles;

        // Pieces of the paragraph between run boundaries, each shaped in its own face.
        std::size_t at = paragraph_start;
        while (at < paragraph_end) {
            const StyledRunSpec* run = nullptr;
            std::size_t piece_end = paragraph_end;
            for (const StyledRunSpec& candidate : runs) {
                if (candidate.begin <= at && at < candidate.end) {
                    run = &candidate;
                    piece_end = std::min(candidate.end, paragraph_end);
                    break;
                }
                if (candidate.begin > at) {
                    piece_end = std::min(piece_end, candidate.begin);
                }
            }
            if (run && run->box > 0) {
                // An attachment's room: one glyph standing for all of the run's text.
                paragraph.glyphs.push_back({.cluster = static_cast<std::uint32_t>(at - paragraph_start),
                                            .advance = run->box,
                                            .break_after = true,
                                            .placeholder = true});
                styles.push_back(run->style);
                at = piece_end;
                continue;
            }
            const FontFace& face = run && run->face ? *run->face : base;
            const std::vector<ShapedParagraph> shaped = shape_text(face, text.substr(at, piece_end - at), &fonts);
            const std::size_t first = paragraph.glyphs.size();
            if (!shaped.empty()) {
                for (ShapedGlyph glyph : shaped.front().glyphs) {
                    glyph.cluster += static_cast<std::uint32_t>(at - paragraph_start);
                    if (!glyph.face && &face != &base) {
                        glyph.face = &face;
                    }
                    paragraph.glyphs.push_back(glyph);
                    styles.push_back(run ? run->style : 0);
                }
            }
            if (run && run->keep_together && paragraph.glyphs.size() > first) {
                for (std::size_t g = first; g + 1 < paragraph.glyphs.size(); ++g) {
                    paragraph.glyphs[g].break_after = false;
                }
            }
            at = piece_end;
        }
        out.paragraphs.push_back(std::move(paragraph));
        out.styles.push_back(std::move(styles));
        out.starts.push_back(paragraph_start);
        if (newline == std::string_view::npos) {
            break;
        }
        paragraph_start = newline + 1;
    }
    return out;
}

std::vector<CaretStop> caret_stops(const StyledShape& shape, const TextLayoutResult& layout, float size,
                                   std::size_t text_size) {
    std::vector<CaretStop> stops;
    for (std::size_t li = 0; li < layout.lines.size(); ++li) {
        const TextLine& line = layout.lines[li];
        const ShapedParagraph& paragraph = shape.paragraphs[line.paragraph];
        const std::size_t start = shape.starts[line.paragraph];
        const std::size_t paragraph_end =
            line.paragraph + 1 < shape.starts.size() ? shape.starts[line.paragraph + 1] - 1 : text_size;
        float x = 0;
        for (std::size_t g = line.begin; g < line.end; ++g) {
            const ShapedGlyph& glyph = paragraph.glyphs[g];
            if (g == line.begin || glyph.cluster != paragraph.glyphs[g - 1].cluster) {
                stops.push_back({start + glyph.cluster, x, li});
            }
            x += glyph.advance * size;
        }
        // The end of the line: where its next glyph (on the next line) starts, or the paragraph's end.
        const std::size_t end_byte = line.end < paragraph.glyphs.size() ? start + paragraph.glyphs[line.end].cluster
                                                                         : paragraph_end;
        if (stops.empty() || stops.back().line != li || stops.back().byte != end_byte) {
            stops.push_back({end_byte, x, li});
        }
    }
    if (stops.empty()) {
        stops.push_back({0, 0, 0});
    }
    return stops;
}

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
                list.commands.push_back(RectCommand{{start - kBackgroundPad, top + 1, pen - start + 2 * kBackgroundPad,
                                                     shape.line_height - 2},
                                                    CornerRadii::all(kBackgroundRadius), ShapeStyle(*look.background),
                                                    Color::clear, 0});
            }
            if (look.underline) {
                list.commands.push_back(
                    RectCommand{{start, snap(baseline + 2), pen - start, 1}, {}, ShapeStyle(look.color), Color::clear, 0});
            }
            if (look.strikethrough) {
                list.commands.push_back(RectCommand{{start, snap(baseline - shape.ascent * 0.3f), pen - start, 1}, {},
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
