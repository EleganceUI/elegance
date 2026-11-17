// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_VIEW_STYLED_DRAW_H
#define ELG_VIEW_STYLED_DRAW_H

// Laid-out styled text (text/styled_layout.h) drawn into a display list.

#include "display/display_list.h"
#include "text/glyph_atlas.h"
#include "text/styled_layout.h"
#include "text/text_layout.h"

#include <elg/color.h>
#include <elg/geometry.h>

#include <optional>
#include <vector>

namespace elg::detail {

struct DrawStyle {
    Color color;
    std::optional<Color> background;
    bool underline = false;
    bool strikethrough = false;
};

// Draws laid-out styled text at `origin` (each line's left edge; `line_x` offsets lines, e.g.
// for alignment). `styles[i]` is how style i looks.
void draw_styled(DisplayList& list, GlyphAtlas& atlas, const FontFace& base, const StyledShape& shape,
                 const TextLayoutResult& layout, const std::vector<DrawStyle>& styles, Point origin, float size,
                 float scale, const std::vector<float>& line_x = {});

} // namespace elg::detail

#endif // ELG_VIEW_STYLED_DRAW_H
