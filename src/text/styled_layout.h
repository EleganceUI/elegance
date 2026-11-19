// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_TEXT_STYLED_LAYOUT_H
#define ELG_TEXT_STYLED_LAYOUT_H

// Text in runs of different fonts and styles (StyledText, styled TextFields): shaped run by run,
// and wrapped as one text. View draws it (view/styled_draw.h).

#include "text/font_library.h"
#include "text/text_layout.h"

#include <elg/color.h>
#include <elg/text.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace elg::detail {

// A run to shape: bytes [begin, end) of the text, in `face`, styled with `style`.
struct StyledRunSpec {
    std::size_t begin = 0;
    std::size_t end = 0;
    const FontFace* face = nullptr;
    std::uint32_t style = 0;     // index into the caller's styles
    bool keep_together = false;  // no line breaks inside (chips)
    float box = 0;               // > 0: one placeholder glyph this many em wide instead of the text
};

struct StyledShape {
    std::vector<ShapedParagraph> paragraphs; // clusters are byte offsets in the paragraph
    std::vector<std::vector<std::uint32_t>> styles; // per paragraph, per glyph: the run's style
    std::vector<std::size_t> starts;          // each paragraph's first byte in the text
    float ascent = 0;      // points: the largest of the faces used
    float line_height = 0; // points
};

// Shapes `text` (split into paragraphs at '\n') with each run's face. Bytes outside every run
// use `base` with style 0.
StyledShape shape_styled(std::string_view text, const std::vector<StyledRunSpec>& runs, const FontFace& base,
                         float size, FontLibrary& fonts);

// A caret position: a byte in the text, where it's drawn (x from the line's start), and its line.
struct CaretStop {
    std::size_t byte = 0;
    float x = 0;
    std::size_t line = 0;
};

// The caret stops of laid-out text: one at each cluster's start and one at each line's end.
std::vector<CaretStop> caret_stops(const StyledShape& shape, const TextLayoutResult& layout, float size,
                                   std::size_t text_size);
// The same for paragraphs shaped on their own (shape_text()): `starts` are their first bytes in
// the text.
std::vector<CaretStop> caret_stops(const std::vector<ShapedParagraph>& paragraphs, const std::vector<std::size_t>& starts,
                                   const TextLayoutResult& layout, float size, std::size_t text_size);

// A run's font: `base` with the style's family, weight, and slant (styled_text.cpp).
Font run_font(const Font& base, const RunStyle& style);

} // namespace elg::detail

#endif // ELG_TEXT_STYLED_LAYOUT_H
