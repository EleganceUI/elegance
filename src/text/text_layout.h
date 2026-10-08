// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_TEXT_TEXT_LAYOUT_H
#define ELG_TEXT_TEXT_LAYOUT_H

#include "text/font_library.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace elg::detail {

// A shaped glyph; advances and offsets are in em units.
struct ShapedGlyph {
    std::uint32_t id = 0;
    std::uint32_t cluster = 0; // byte offset (in the paragraph) of the text it came from
    float advance = 0;
    float x_offset = 0;
    float y_offset = 0;
    bool is_space = false;    // whitespace: trimmed at line ends
    bool break_after = false; // a line may break after this glyph
};

// One paragraph (text between newlines), shaped once at mount.
struct ShapedParagraph {
    std::vector<ShapedGlyph> glyphs;
};

// Splits at '\n' and shapes each paragraph with HarfBuzz.
// TODO: bidi, script itemization, font fallback, UAX #14 line breaking (design.md §11).
std::vector<ShapedParagraph> shape_text(const FontFace& face, std::string_view text);

struct TextLine {
    std::size_t paragraph = 0;
    std::size_t begin = 0; // glyph range within the paragraph
    std::size_t end = 0;
    float width = 0;       // points, without trailing spaces, including any ellipsis
    bool ellipsis = false; // draw the ellipsis after the glyphs
};

struct TextLayoutResult {
    std::vector<TextLine> lines;
    float width = 0;  // widest line, points
    float height = 0; // points
};

struct TextLayoutParams {
    float font_size = 15;    // points
    float line_height = 20;  // points
    float ellipsis_width = 0;
    std::optional<float> max_width;
    std::optional<float> max_height;
    int line_limit = 0; // 0: unlimited
};

// Greedy word wrapping (breaking inside words only when a word doesn't fit on a line by itself),
// then truncation with an ellipsis to the line limit and the height available.
TextLayoutResult layout_text(const std::vector<ShapedParagraph>& paragraphs, const TextLayoutParams& params);

} // namespace elg::detail

#endif // ELG_TEXT_TEXT_LAYOUT_H
