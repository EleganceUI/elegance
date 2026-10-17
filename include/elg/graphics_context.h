// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_GRAPHICS_CONTEXT_H
#define ELG_GRAPHICS_CONTEXT_H

#include <elg/color.h>
#include <elg/export.h>
#include <elg/geometry.h>
#include <elg/gradient.h>
#include <elg/path.h>

namespace elg {

namespace detail {
class DisplayList;
}

// Immediate-mode drawing inside a Canvas view (design.md §14). Coordinates are logical points
// relative to the canvas's top-left corner.
class ELG_API GraphicsContext {
public:
    // Internal: created by the framework for each draw.
    GraphicsContext(detail::DisplayList& list, Rect bounds, float scale) noexcept;

    // Drawable area in logical points.
    Size size() const noexcept;
    // Pixels per logical point.
    float scale() const noexcept;

    // Animated drawing: seconds on the frame clock (the same instant for everything drawn in a
    // frame), and a request to be drawn again next frame. Call request_frame() on every draw
    // while it should keep moving; the window goes back to idle once nothing asks.
    //   Canvas([](GraphicsContext& g) { draw_spinner(g, g.time()); g.request_frame(); })
    double time() const noexcept;
    void request_frame() noexcept;

    void fill_rect(Rect rect, Color color, CornerRadii radii = {});
    void fill_rect(Rect rect, Color color, float radius) {
        fill_rect(rect, color, CornerRadii::all(radius));
    }

    // Draws a border of `width` points inside `rect`.
    void stroke_rect(Rect rect, Color color, float width, CornerRadii radii = {});

    // Fill and border in one primitive.
    void draw_rect(Rect rect, CornerRadii radii, Color fill, Color border, float border_width);

    // Vector paths, in the canvas's points (design.md §5).
    void fill_path(const Path& path, ShapeStyle style, FillRule rule = FillRule::non_zero);
    void stroke_path(const Path& path, Color color, const StrokeStyle& style);

private:
    detail::DisplayList* list_;
    Rect bounds_;
    float scale_;
};

} // namespace elg

#endif // ELG_GRAPHICS_CONTEXT_H
