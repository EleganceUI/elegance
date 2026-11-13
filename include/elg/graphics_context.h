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

/// Immediate-mode drawing inside a Canvas view (design.md §14). Coordinates are logical points
/// relative to the canvas's top-left corner. Valid only during the draw call it's passed to.
class ELG_API GraphicsContext {
public:
    /// Internal: created by the framework for each draw.
    ///
    /// @param list Receives the drawing commands; must outlive the context.
    /// @param bounds The canvas's frame, in window points.
    /// @param scale Pixels per logical point.
    GraphicsContext(detail::DisplayList& list, Rect bounds, float scale) noexcept;
    /// Closes any clips still pushed.
    ~GraphicsContext();
    GraphicsContext(const GraphicsContext&) = delete;
    GraphicsContext& operator=(const GraphicsContext&) = delete;
    GraphicsContext(GraphicsContext&&) = delete;
    GraphicsContext& operator=(GraphicsContext&&) = delete;

    /// @return The drawable area in logical points: the canvas's size.
    [[nodiscard]] Size size() const noexcept;
    /// @return Pixels per logical point.
    [[nodiscard]] float scale() const noexcept;

    /// Animated drawing: seconds on the frame clock (the same instant for everything drawn in a
    /// frame). Pair it with request_frame().
    ///
    /// @code
    /// Canvas([](GraphicsContext& g) { draw_spinner(g, g.time()); g.request_frame(); })
    /// @endcode
    ///
    /// @return The frame clock, in seconds.
    [[nodiscard]] static double time() noexcept;
    /// A request to be drawn again next frame. Call request_frame() on every draw while it should
    /// keep moving; the window goes back to idle once nothing asks.
    void request_frame() noexcept;

    /// Fills a rectangle. Draws nothing when `rect` has no width or height.
    ///
    /// @param rect The rectangle.
    /// @param color The fill color.
    /// @param radii The corner radii in points, each at most half the rect's shorter side when drawn.
    void fill_rect(Rect rect, Color color, CornerRadii radii = {});
    /// Like fill_rect(Rect, Color, CornerRadii), but with the same radius on every corner.
    ///
    /// @param rect The rectangle.
    /// @param color The fill color.
    /// @param radius The corner radius in points.
    void fill_rect(Rect rect, Color color, float radius) {
        fill_rect(rect, color, CornerRadii::all(radius));
    }

    /// Draws a border of `width` points inside `rect`. Draws nothing when `rect` has no width or
    /// height.
    ///
    /// @param rect The rectangle whose edge the border follows.
    /// @param color The border color.
    /// @param width The border width in points; negative counts as 0.
    /// @param radii The outer corner radii in points.
    void stroke_rect(Rect rect, Color color, float width, CornerRadii radii = {});

    /// Fill and border in one primitive. Draws nothing when `rect` has no width or height.
    ///
    /// @param rect The rectangle.
    /// @param radii The outer corner radii in points.
    /// @param fill The fill color; Color::clear for none.
    /// @param border The border color.
    /// @param border_width The border width in points, inside `rect`; negative counts as 0.
    void draw_rect(Rect rect, CornerRadii radii, Color fill, Color border, float border_width);

    /// Fills a vector path, in the canvas's points (design.md §5). Every subpath counts as closed.
    ///
    /// @param path The outline.
    /// @param style A color or gradient.
    /// @param rule Which points the outline encloses where it overlaps itself.
    void fill_path(const Path& path, ShapeStyle style, FillRule rule = FillRule::non_zero);
    /// Strokes a vector path, in the canvas's points (design.md §5).
    ///
    /// @param path The line to draw.
    /// @param color The line's color.
    /// @param style The line's width, caps, joins and dashes.
    void stroke_path(const Path& path, Color color, const StrokeStyle& style);

    /// Drawing until the matching pop_clip() shows only inside `rect` (with rounded corners).
    /// Clips nest; ones left pushed are popped when the drawing ends.
    ///
    /// @param rect The visible area, in the canvas's points.
    /// @param radii The clip's corner radii in points.
    void push_clip(Rect rect, CornerRadii radii = {});
    /// Ends the innermost clip from push_clip(). Does nothing when no clip is pushed.
    void pop_clip();

private:
    detail::DisplayList* list_;
    Rect bounds_;
    float scale_;
    int clips_ = 0;
};

} // namespace elg

#endif // ELG_GRAPHICS_CONTEXT_H
