// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include "text/text_layout.h"

#include <hb.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace elg::detail {

namespace {

// Absorbs float error so text measured at width W also fits when laid out at exactly W.
constexpr float kFitTolerance = 0.01f;

ShapedParagraph shape_paragraph(const FontFace& face, std::string_view text) {
    ShapedParagraph paragraph;
    if (text.empty()) {
        return paragraph;
    }

    hb_buffer_t* buffer = hb_buffer_create();
    hb_buffer_add_utf8(buffer, text.data(), static_cast<int>(text.size()), 0, static_cast<int>(text.size()));
    hb_buffer_guess_segment_properties(buffer);
    hb_shape(face.hb_font(), buffer, nullptr, 0);

    unsigned count = 0;
    const hb_glyph_info_t* infos = hb_buffer_get_glyph_infos(buffer, &count);
    const hb_glyph_position_t* positions = hb_buffer_get_glyph_positions(buffer, &count);
    const float em = face.units_per_em();

    paragraph.glyphs.reserve(count);
    for (unsigned i = 0; i < count; ++i) {
        // The cluster is the byte offset of the first character the glyph came from.
        const char c = infos[i].cluster < text.size() ? text[infos[i].cluster] : '\0';
        const bool space = c == ' ' || c == '\t';
        paragraph.glyphs.push_back({
            .id = infos[i].codepoint, // after shaping, this is the glyph index
            .advance = static_cast<float>(positions[i].x_advance) / em,
            .x_offset = static_cast<float>(positions[i].x_offset) / em,
            .y_offset = static_cast<float>(positions[i].y_offset) / em,
            .is_space = space,
            .break_after = space || c == '-',
        });
    }
    hb_buffer_destroy(buffer);
    return paragraph;
}

// Width in points of glyphs [begin, end), ignoring trailing whitespace.
float line_width(const ShapedParagraph& paragraph, std::size_t begin, std::size_t end, float size) {
    while (end > begin && paragraph.glyphs[end - 1].is_space) {
        --end;
    }
    float width = 0;
    for (std::size_t i = begin; i < end; ++i) {
        width += paragraph.glyphs[i].advance * size;
    }
    return width;
}

void wrap_paragraph(const ShapedParagraph& paragraph, std::size_t index, float size, float max_width,
                    std::vector<TextLine>& lines) {
    const std::vector<ShapedGlyph>& glyphs = paragraph.glyphs;
    if (glyphs.empty()) {
        lines.push_back({index, 0, 0, 0, false}); // an empty line, e.g. from "\n\n"
        return;
    }

    std::size_t start = 0;
    while (start < glyphs.size()) {
        // Wrapped lines don't start with the whitespace they broke at.
        if (start > 0) {
            while (start < glyphs.size() && glyphs[start].is_space) {
                ++start;
            }
            if (start == glyphs.size()) {
                break;
            }
        }

        std::size_t end = glyphs.size();
        std::size_t last_break = glyphs.size(); // none yet
        float width = 0;
        for (std::size_t i = start; i < glyphs.size(); ++i) {
            const float advance = glyphs[i].advance * size;
            if (width + advance > max_width + kFitTolerance && i > start && !glyphs[i].is_space) {
                // Break at the last opportunity; if there's none, break inside the word.
                end = last_break < glyphs.size() ? last_break + 1 : i;
                break;
            }
            width += advance;
            if (glyphs[i].break_after) {
                last_break = i;
            }
        }
        lines.push_back({index, start, end, line_width(paragraph, start, end, size), false});
        start = end;
    }
}

} // namespace

std::vector<ShapedParagraph> shape_text(const FontFace& face, std::string_view text) {
    std::vector<ShapedParagraph> paragraphs;
    std::size_t start = 0;
    while (true) {
        const std::size_t newline = text.find('\n', start);
        std::string_view line = text.substr(start, newline == std::string_view::npos ? std::string_view::npos
                                                                                     : newline - start);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        paragraphs.push_back(shape_paragraph(face, line));
        if (newline == std::string_view::npos) {
            break;
        }
        start = newline + 1;
    }
    return paragraphs;
}

TextLayoutResult layout_text(const std::vector<ShapedParagraph>& paragraphs, const TextLayoutParams& params) {
    const float max_width = params.max_width && std::isfinite(*params.max_width)
                                ? std::max(0.f, *params.max_width)
                                : std::numeric_limits<float>::infinity();

    TextLayoutResult result;
    for (std::size_t i = 0; i < paragraphs.size(); ++i) {
        wrap_paragraph(paragraphs[i], i, params.font_size, max_width, result.lines);
    }

    // How many lines fit: the line limit and the proposed height (at least one line).
    std::size_t max_lines = params.line_limit > 0 ? static_cast<std::size_t>(params.line_limit)
                                                  : std::numeric_limits<std::size_t>::max();
    if (params.max_height && std::isfinite(*params.max_height) && params.line_height > 0) {
        const auto fit = static_cast<std::size_t>(std::floor((*params.max_height + kFitTolerance) / params.line_height));
        max_lines = std::min(max_lines, std::max<std::size_t>(fit, 1));
    }

    if (result.lines.size() > max_lines) {
        result.lines.resize(max_lines);
        TextLine& last = result.lines.back();
        const ShapedParagraph& paragraph = paragraphs[last.paragraph];
        // Drop glyphs until the ellipsis fits.
        while (last.end > last.begin &&
               line_width(paragraph, last.begin, last.end, params.font_size) + params.ellipsis_width >
                   max_width + kFitTolerance) {
            --last.end;
        }
        while (last.end > last.begin && paragraph.glyphs[last.end - 1].is_space) {
            --last.end; // "word…", not "word …"
        }
        last.width = line_width(paragraph, last.begin, last.end, params.font_size) + params.ellipsis_width;
        last.ellipsis = true;
    }

    for (const TextLine& line : result.lines) {
        result.width = std::max(result.width, line.width);
    }
    result.height = static_cast<float>(result.lines.size()) * params.line_height;
    return result;
}

} // namespace elg::detail
