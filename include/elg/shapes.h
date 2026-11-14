// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_SHAPES_H
#define ELG_SHAPES_H

#include <elg/color.h>
#include <elg/export.h>
#include <elg/geometry.h>
#include <elg/gradient.h>
#include <elg/path.h>
#include <elg/prop.h>
#include <elg/view.h>

#include <functional>
#include <memory>
#include <optional>

namespace elg {

namespace detail {
class ShapeNode;
}

/// A view that draws a shape filling its frame (taking the whole proposed size, like SwiftUI; 10 points
/// in a dimension the proposal leaves open). Shape modifiers return Shape, so they chain until a generic
/// modifier turns it into a View:
///
/// @code
/// RoundedRectangle(12).fill(Color::hex(0x8AADF4)).stroke_border(Color::white, 2).padding(8)
/// @endcode
///
/// Members are exported individually, like View's (see view.h).
class Shape : public View {
public:
    /// Fills the shape. Shapes with neither a fill nor a stroke use the environment's foreground; a
    /// stroked shape isn't filled unless asked to be.
    ///
    /// @param style A color or gradient, or a closure returning one.
    /// @return This shape, filled with `style`.
    [[nodiscard]] ELG_API Shape fill(Prop<ShapeStyle> style) const;
    /// A line centered on the shape's edge, so half of it falls outside the frame. Replaces any earlier
    /// stroke.
    ///
    /// @param color The line's color.
    /// @param line_width The line's width in points.
    /// @return This shape, stroked.
    [[nodiscard]] ELG_API Shape stroke(Prop<Color> color, float line_width = 1) const;
    /// A line with caps, joins, and dashes, centered on the shape's edge. Replaces any earlier stroke.
    ///
    /// @code
    /// .stroke(ThemeColor::outline, {.width = 2, .dash = {4, 4}})
    /// @endcode
    ///
    /// @param color The line's color.
    /// @param style The line's width, caps, joins and dash pattern.
    /// @return This shape, stroked.
    [[nodiscard]] ELG_API Shape stroke(Prop<Color> color, StrokeStyle style) const;
    /// A line inside the shape's edge. PathShape and CustomShape have no inside to move it to: their
    /// line is centered, as with stroke(). Replaces any earlier stroke.
    ///
    /// @param color The line's color.
    /// @param line_width The line's width in points.
    /// @return This shape, stroked.
    [[nodiscard]] ELG_API Shape stroke_border(Prop<Color> color, float line_width = 1) const;

    /// Internal: created by the shape functions below.
    ///
    /// @param node The shape's description.
    ELG_API explicit Shape(std::shared_ptr<const detail::ShapeNode> node);
};

/// A rectangle filling its frame.
///
/// @return The shape.
ELG_API Shape Rectangle();
/// A rectangle filling its frame with the same rounding on every corner.
///
/// @param corner_radius The corner radius in points, at most half the frame's shorter side when drawn.
/// @return The shape.
ELG_API Shape RoundedRectangle(float corner_radius);
/// Like RoundedRectangle(float), but with a radius per corner.
///
/// @param radii The corner radii in points, each at most half the frame's shorter side when drawn.
/// @return The shape.
ELG_API Shape RoundedRectangle(CornerRadii radii);
/// A rectangle filling its frame with fully rounded ends along the shorter side.
///
/// @return The shape.
ELG_API Shape Capsule();
/// The largest circle that fits, centered.
///
/// @return The shape.
ELG_API Shape Circle();

/// Options for PathShape().
struct PathShapeOptions {
    /// The path's own coordinate box (as in SVG): the path is scaled to fit the frame (keeping its
    /// aspect ratio) and centered. Without it, path coordinates are points from the frame's corner.
    /// A box with no width or height counts as none.
    std::optional<Rect> view_box;
    FillRule fill_rule = FillRule::non_zero; ///< Which points the path's outline encloses.
};

/// A vector path as a shape (design.md §5). Every subpath counts as closed for filling.
///
/// @code
/// PathShape(Path::svg("M12 2 L22 21 H2 Z"), {.view_box = Rect{0, 0, 24, 24}}).fill(palette::peach)
/// @endcode
///
/// @param path The outline.
/// @param options The view box and fill rule.
/// @return The shape.
ELG_API Shape PathShape(Path path, PathShapeOptions options = {});

/// A shape whose path depends on its frame, rebuilt when the frame's size changes (SwiftUI's
/// Shape.path(in:)).
///
/// @code
/// CustomShape([](Rect r) { return Path().move_to({0, r.height}).line_to({r.width / 2, 0})
///                                       .line_to({r.width, r.height}).close(); })
/// @endcode
///
/// @param path Builds the outline. It gets the frame's size as a rect at (0, 0) and returns the path in
///   points from the frame's corner. May be empty: the shape is then empty too.
/// @param fill_rule Which points the path's outline encloses.
/// @return The shape.
ELG_API Shape CustomShape(std::function<Path(Rect)> path, FillRule fill_rule = FillRule::non_zero);

} // namespace elg

#endif // ELG_SHAPES_H
