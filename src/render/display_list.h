// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_RENDER_DISPLAY_LIST_H
#define ELG_RENDER_DISPLAY_LIST_H

#include <elg/color.h>
#include <elg/geometry.h>
#include <elg/gradient.h>
#include <elg/modifier.h>
#include <elg/path.h>
#include <elg/view.h>

#include "media/image_decode.h"
#include "media/video_source.h"

#include <array>
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

// An analytic shadow inside a rounded rect: the shape's own edge, moved by `offset` and grown
// inward by `spread`, blurred, drawn inside the shape.
struct InnerShadowCommand {
    Rect rect; // the shape
    CornerRadii radii;
    Color color;
    float blur_radius = 0; // points; sigma is half of it
    Point offset;
    float spread = 0;
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

// A raster image (design.md §13.3) drawn into `rect`, showing the `uv` part of it (normalized).
struct ImageCommand {
    Rect rect;
    Rect uv{0, 0, 1, 1};
    std::shared_ptr<const DecodedImage> image; // shared with the render thread; immutable
    float opacity = 1;
};

// The current frame of a video (design.md §13.3), drawn into `rect`. The render thread takes new
// frames from the source itself, so a playing video redraws without new display lists.
struct VideoCommand {
    Rect rect;
    Rect uv{0, 0, 1, 1};
    std::shared_ptr<VideoSource> source;
    float opacity = 1;
};

// Clips everything until the matching PopClipCommand to a rounded rect.
struct PushClipCommand {
    Rect rect;
    CornerRadii radii;
};

struct PopClipCommand {};

// A color matrix over sRGB with straight alpha: four rows (red, green, blue, alpha out), each
// four coefficients (red, green, blue, alpha in) and an offset.
using ColorMatrix = std::array<float, 20>;

// Rotates and scales a layer about `pivot` (window points): p' = pivot + M (p - pivot), with
// x' = a x + c y and y' = b x + d y.
struct LayerTransform {
    float a = 1, b = 0, c = 0, d = 1;
    Point pivot;
};

// Renders everything until the matching PopLayerCommand offscreen, then composites it with
// `opacity` and an optional blurred shadow of its alpha. With a `mask`, only what the mask path
// covers is kept (clip_shape with a path). The rest: a blur of the content, a color matrix, a
// blend mode with what's behind, a transform, and a shadow inside it.
struct PushLayerCommand {
    Rect bounds; // of the layer's content, before the shadow
    float opacity = 1;
    std::optional<Shadow> shadow{};
    std::optional<PathCommand> mask{}; // its fill is ignored
    float blur = 0;                  // radius in points
    bool blur_opaque = false;        // edges read as the nearest pixel, not transparent
    std::optional<ColorMatrix> color_matrix{};
    BlendMode blend = BlendMode::normal;
    std::optional<LayerTransform> transform{};
    std::optional<Shadow> inner_shadow{};
};

// Inside a layer: the commands after it (to the PopLayerCommand) are a mask; only where they're
// opaque does the content before it show (View::mask).
struct LayerMaskCommand {};

struct PopLayerCommand {};

using DrawCommand = std::variant<RectCommand, GlyphCommand, BoxShadowCommand, InnerShadowCommand, PathCommand,
                                 ImageCommand, VideoCommand, PushClipCommand, PopClipCommand, PushLayerCommand,
                                 LayerMaskCommand, PopLayerCommand>;

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
    // While painting: the part that can be seen (a scroll view's frame); elements entirely
    // outside it aren't painted. nullopt: everything is.
    std::optional<Rect> cull;
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
// The matrix for a color filter (straight sRGB), and applying one to a color.
ColorMatrix color_matrix(ColorFilter::Kind kind, float amount, Color color);
Color apply(const ColorMatrix& matrix, Color color);
// Where `rect` lands under `transform`: the bounds of its four corners.
Rect transformed_bounds(const LayerTransform& transform, const Rect& rect);

} // namespace elg::detail

#endif // ELG_RENDER_DISPLAY_LIST_H
