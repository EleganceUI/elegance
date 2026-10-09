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

// Decodes the UTF-8 character at `at` (advancing it). Invalid bytes decode as U+FFFD.
char32_t decode(std::string_view text, std::size_t& at) {
    const auto byte = [&](std::size_t i) { return static_cast<unsigned char>(text[i]); };
    const unsigned char lead = byte(at);
    int extra = lead < 0x80 ? 0 : lead >= 0xF0 ? 3 : lead >= 0xE0 ? 2 : lead >= 0xC0 ? 1 : -1;
    if (extra < 0 || at + static_cast<std::size_t>(extra) >= text.size() + (extra == 0 ? 1 : 0)) {
        ++at;
        return 0xFFFD;
    }
    char32_t code = extra == 0 ? lead : lead & (0x3F >> extra);
    for (int i = 1; i <= extra; ++i) {
        code = (code << 6) | (byte(at + static_cast<std::size_t>(i)) & 0x3F);
    }
    at += static_cast<std::size_t>(extra) + 1;
    return code;
}

// Characters that join the one before them rather than choosing a font of their own.
bool is_joiner(char32_t c) {
    return (c >= 0x0300 && c <= 0x036F) || (c >= 0xFE00 && c <= 0xFE0F) || c == 0x200D || (c >= 0x1F3FB && c <= 0x1F3FF) ||
           (c >= 0xE0100 && c <= 0xE01EF);
}

// CJK text breaks between any two characters.
bool breaks_anywhere(char32_t c) {
    return (c >= 0x2E80 && c <= 0x9FFF) || (c >= 0xAC00 && c <= 0xD7AF) || (c >= 0xF900 && c <= 0xFAFF) ||
           (c >= 0xFF00 && c <= 0xFFEF) || (c >= 0x20000 && c <= 0x3FFFF);
}

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
        std::size_t at = infos[i].cluster;
        const char32_t code = at < text.size() ? decode(text, at) : 0;
        paragraph.glyphs.push_back({
            .id = infos[i].codepoint, // after shaping, this is the glyph index
            .cluster = infos[i].cluster,
            .advance = static_cast<float>(positions[i].x_advance) / em,
            .x_offset = static_cast<float>(positions[i].x_offset) / em,
            .y_offset = static_cast<float>(positions[i].y_offset) / em,
            .is_space = space,
            .break_after = space || c == '-' || breaks_anywhere(code),
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

// Re-shapes the parts `face` has no glyphs for (glyph 0) with fallback fonts.
ShapedParagraph shape_with_fallback(const FontFace& face, std::string_view text, FontLibrary& fonts) {
    ShapedParagraph shaped = shape_paragraph(face, text);
    if (face.icon() || std::ranges::none_of(shaped.glyphs, [](const ShapedGlyph& g) { return g.id == 0; })) {
        return shaped;
    }
    const std::vector<ShapedGlyph>& glyphs = shaped.glyphs;
    ShapedParagraph result;
    result.glyphs.reserve(glyphs.size());
    std::size_t i = 0;
    while (i < glyphs.size()) {
        if (glyphs[i].id != 0) {
            result.glyphs.push_back(glyphs[i++]);
            continue;
        }
        // A run of missing glyphs: the text it covers (clusters increase left to right).
        // Glyphs sharing a cluster belong together (e.g. an emoji and its invisible variation
        // selector), so the run takes them too.
        std::size_t j = i;
        while (j < glyphs.size() && (glyphs[j].id == 0 || (j > i && glyphs[j].cluster == glyphs[j - 1].cluster))) {
            ++j;
        }
        const std::size_t begin = glyphs[i].cluster;
        const std::size_t end = j < glyphs.size() ? glyphs[j].cluster : text.size();
        if (end <= begin) {
            result.glyphs.insert(result.glyphs.end(), glyphs.begin() + static_cast<std::ptrdiff_t>(i),
                                 glyphs.begin() + static_cast<std::ptrdiff_t>(j));
            i = j;
            continue;
        }
        // Split it into pieces by the first fallback font that has each character, and shape each
        // piece with its font. Characters nobody has stay as the primary font's missing glyph.
        std::size_t at = begin;
        while (at < end) {
            std::size_t next = at;
            const char32_t first = decode(text, next);
            const FontFace* chosen = fonts.fallback_for(first);
            if (!chosen && is_joiner(first)) {
                at = next; // a variation selector or joiner no font draws: it has no look of its own
                continue;
            }
            std::size_t piece_end = next;
            while (piece_end < end) {
                std::size_t peek = piece_end;
                const char32_t c = decode(text, peek);
                if (!is_joiner(c) && fonts.fallback_for(c) != chosen) {
                    break;
                }
                piece_end = peek;
            }
            const FontFace& piece_face = chosen ? *chosen : face;
            ShapedParagraph piece = shape_paragraph(piece_face, text.substr(at, piece_end - at));
            for (ShapedGlyph& glyph : piece.glyphs) {
                glyph.cluster += static_cast<std::uint32_t>(at);
                glyph.face = chosen;
                result.glyphs.push_back(glyph);
            }
            at = piece_end;
        }
        i = j;
    }
    return result;
}

std::vector<ShapedParagraph> shape_text(const FontFace& face, std::string_view text, FontLibrary* fonts) {
    std::vector<ShapedParagraph> paragraphs;
    std::size_t start = 0;
    while (true) {
        const std::size_t newline = text.find('\n', start);
        std::string_view line = text.substr(start, newline == std::string_view::npos ? std::string_view::npos
                                                                                     : newline - start);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        paragraphs.push_back(fonts ? shape_with_fallback(face, line, *fonts) : shape_paragraph(face, line));
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
