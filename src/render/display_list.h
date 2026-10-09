// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_RENDER_DISPLAY_LIST_H
#define ELG_RENDER_DISPLAY_LIST_H

#include <elg/color.h>
#include <elg/geometry.h>
#include <elg/gradient.h>
#include <elg/path.h>
#include <elg/view.h>

#include <memory>
#include <optional>
#include <variant>
#include <vector>

namespace elg::detail {

struct RectCommand {
    Rect rect;
    CornerRadii radii;
    ShapeStyle fill;
    Color border;
    float border_width = 0;
};

struct GlyphCommand {
    Rect quad;           // logical points
    Rect uv;             // normalized atlas coordinates
    Color color;
    float em_size = 15;  // font size in points; sets the distance field's on-screen range
};

// An analytic blurred rounded-rect shadow: the fast path for shadows of single shapes.
struct BoxShadowCommand {
    Rect rect; // the shape, already offset
    CornerRadii radii;
    Color color;
    float blur_radius = 0; // points; sigma is half of it
};

// A filled vector path (design.md §5). The polygons (flattened, closed) are shared and in local
// points; `scale` then `offset` place them in the window, so moving or scaling a path (scrolling,
// transitions) doesn't touch them.
struct PathCommand {
    std::shared_ptr<const std::vector<std::vector<Point>>> polygons;
    Rect bounds; // of the polygons, local points
    float scale = 1;
    Point offset;
    Rect paint_frame; // window points; gradients' unit points map across it
    ShapeStyle fill;
    FillRule rule = FillRule::non_zero;

    Rect window_bounds() const {
        return {bounds.x * scale + offset.x, bounds.y * scale + offset.y, bounds.width * scale, bounds.height * scale};
    }
};

// Clips everything until the matching PopClipCommand to a rounded rect.
struct PushClipCommand {
    Rect rect;
    CornerRadii radii;
};

struct PopClipCommand {};

// Renders everything until the matching PopLayerCommand offscreen, then composites it with
// `opacity` and an optional blurred shadow of its alpha. With a `mask`, only what the mask path
// covers is kept (clip_shape with a path).
struct PushLayerCommand {
    Rect bounds; // of the layer's content, before the shadow
    float opacity = 1;
    std::optional<Shadow> shadow;
    std::optional<PathCommand> mask; // its fill is ignored
};

struct PopLayerCommand {};

using DrawCommand = std::variant<RectCommand, GlyphCommand, BoxShadowCommand, PathCommand, PushClipCommand,
                                 PopClipCommand, PushLayerCommand, PopLayerCommand>;

// Immutable-once-built draw commands for one window frame, in logical points and sRGB, in
// painter's order. Holds copies of everything, never references into app memory
// (design.md §9.6, rule 1).
class DisplayList {
public:
    Color clear_color = Color::white;
    float scale = 1; // pixels per logical point
    std::vector<DrawCommand> commands;
    // Set while painting when something is mid-animation: the window needs another frame.
    bool animating = false;
};

// --- Helpers ---

bool is_clear(const ShapeStyle& style);
ShapeStyle with_opacity(const ShapeStyle& style, float opacity);
// The area a command can draw into, in points; nullopt for structural commands (clip, pop).
std::optional<Rect> draw_bounds(const DrawCommand& command);
Rect union_of(const Rect& a, const Rect& b);
// Moves commands by (dx, dy) and scales them by `scale` around `origin` (transitions).
void transform(std::vector<DrawCommand>& commands, float dx, float dy, float scale, Point origin);
// Appends `content` faded to `opacity` as a group: an alpha multiply for a single shape, an
// offscreen layer otherwise.
void append_faded(DisplayList& list, DisplayList&& content, float opacity);

} // namespace elg::detail

#endif // ELG_RENDER_DISPLAY_LIST_H
