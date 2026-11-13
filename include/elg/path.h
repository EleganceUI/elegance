// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_PATH_H
#define ELG_PATH_H

// Vector paths (design.md §5): geometry built from lines and curves, filled or stroked by the GPU.
//
//   Path triangle;
//   triangle.move_to({0, 10}).line_to({10, 10}).line_to({5, 0}).close();
//   PathShape(triangle).fill(ThemeColor::accent)
//   PathShape(Path::svg("M12 2 L22 22 H2 Z"), {.view_box = Rect{0, 0, 24, 24}}).stroke(ThemeColor::primary, {.width = 2})

#include <elg/export.h>
#include <elg/geometry.h>

#include <cstdint>
#include <string_view>
#include <vector>

namespace elg {

/// Which points a path's outline encloses where it overlaps itself.
enum class FillRule {
    non_zero, ///< inside if the outline winds around the point at all (overlaps stay filled)
    even_odd, ///< inside if a ray from the point crosses the outline an odd number of times (holes)
};

/// How a stroke ends at the open ends of a subpath.
enum class LineCap {
    butt,   ///< ends flat at the endpoint
    round,  ///< a half disc beyond the endpoint
    square  ///< a square beyond the endpoint, half the line width long
};
/// How a stroke turns a corner.
enum class LineJoin {
    miter, ///< a sharp point, or a bevel past StrokeStyle::miter_limit
    round, ///< a disc around the corner
    bevel  ///< the corner cut off straight
};

/// How a path is stroked: width, caps, joins and dashes.
struct StrokeStyle {
    float width = 1;                 ///< line width in points
    LineCap cap = LineCap::butt;     ///< how open ends (and each dash's ends) look
    LineJoin join = LineJoin::miter; ///< how corners look
    float miter_limit = 4;           ///< miter joins longer than this many widths become bevels
    /// on, off, on, off... lengths; empty: solid. An odd number of lengths is used written twice, as
    /// in SVG: {4} is {4, 4}, and {4, 2, 1} is {4, 2, 1, 4, 2, 1}.
    std::vector<float> dash;
    float dash_phase = 0;            ///< how far into the pattern the stroke starts
};

/// A vector path: subpaths of lines and curves, built with the chaining methods below or parsed
/// from SVG path data, and drawn with PathShape() or GraphicsContext. Coordinates are points, y down.
class ELG_API Path {
public:
    /// One flattened subpath: points joined by straight lines.
    struct Polyline {
        std::vector<Point> points; ///< The points in order, without repeats.
        /// Whether the subpath ends with close(): the last point connects back to the first, which
        /// isn't repeated at the end.
        bool closed = false;
    };

    Path() = default;

    // --- Building (each returns *this) ---

    /// Starts a new subpath.
    ///
    /// @param point Where the subpath starts.
    /// @return This path.
    Path& move_to(Point point);
    /// A straight line from the current point. Without an open subpath (at the start, or after
    /// close()), one starts at the current point first: (0, 0), or the closed subpath's start.
    ///
    /// @param point Where the line ends; the new current point.
    /// @return This path.
    Path& line_to(Point point);
    /// A quadratic Bézier curve from the current point, starting a subpath like line_to() if none is
    /// open.
    ///
    /// @param control The control point.
    /// @param end Where the curve ends; the new current point.
    /// @return This path.
    Path& quad_to(Point control, Point end);
    /// A cubic Bézier curve from the current point, starting a subpath like line_to() if none is open.
    ///
    /// @param control1 The control point near the start.
    /// @param control2 The control point near the end.
    /// @param end Where the curve ends; the new current point.
    /// @return This path.
    Path& cubic_to(Point control1, Point control2, Point end);
    /// An elliptical arc to `end`, as in SVG (rx, ry, x-axis rotation in degrees, flags), stored as
    /// cubic curves of at most a quarter turn each. Does nothing when `end` is the current point, and
    /// draws a straight line when either radius is 0.
    ///
    /// @param rx The ellipse's horizontal radius; its sign is ignored.
    /// @param ry The ellipse's vertical radius; its sign is ignored. Radii too small to reach `end`
    ///   are scaled up just enough.
    /// @param rotation_degrees The ellipse's x axis rotation, in degrees.
    /// @param large_arc Whether to take the arc of more than 180 degrees.
    /// @param sweep Whether the arc runs in the direction of increasing angle (clockwise on screen).
    /// @param end Where the arc ends; the new current point.
    /// @return This path.
    Path& arc_to(float rx, float ry, float rotation_degrees, bool large_arc, bool sweep, Point end);
    /// Closes the open subpath with a line back to its start, which becomes the current point. Does
    /// nothing when no subpath is open.
    ///
    /// @return This path.
    Path& close();

    /// Adds `rect` as a closed subpath, clockwise on screen from its top-left corner.
    ///
    /// @param rect The rectangle.
    /// @return This path.
    Path& add_rect(Rect rect);
    /// Adds a rounded rectangle as a closed subpath.
    ///
    /// @param rect The rectangle.
    /// @param radius The corner radius, clamped to [0, half the shorter side]; 0 adds a plain rect.
    /// @return This path.
    Path& add_rounded_rect(Rect rect, float radius);
    /// Adds the ellipse inscribed in `rect` as a closed subpath.
    ///
    /// @param rect The ellipse's bounding box.
    /// @return This path.
    Path& add_ellipse(Rect rect);
    /// Adds a circle as a closed subpath.
    ///
    /// @param center The circle's center.
    /// @param radius The circle's radius.
    /// @return This path.
    Path& add_circle(Point center, float radius);
    /// Appends the subpaths of `other`. Its current point and open subpath, if any, become this
    /// path's.
    ///
    /// @param other The path to append.
    /// @return This path.
    Path& add_path(const Path& other);

    /// Parses SVG path data ("M10 10 L20 20 Z", all commands, relative and absolute).
    ///
    /// @param data The path data, as in an SVG `d` attribute.
    /// @return The path.
    /// @throws UsageError With the position of anything it can't read, including numbers that aren't
    ///   finite or are larger than 10^9.
    static Path svg(std::string_view data);

    // --- Queries ---

    /// @return Whether the path has no subpaths.
    [[nodiscard]] bool empty() const noexcept { return verbs_.empty(); }
    /// The smallest rect around the path's outline (curves included, not their control points),
    /// measured on the curves flattened to 0.1 points.
    ///
    /// @return The bounds, or an empty rect at (0, 0) for an empty path.
    [[nodiscard]] Rect bounds() const;
    /// Hit tests the filled path. Every subpath counts as closed, as in SVG.
    ///
    /// @param point The point to test, in the path's coordinates.
    /// @param rule Which points the outline encloses where it overlaps itself.
    /// @return Whether `point` is inside.
    [[nodiscard]] bool contains(Point point, FillRule rule = FillRule::non_zero) const;

    /// Scaled, then moved: (x * scale_x + dx, y * scale_y + dy).
    ///
    /// @param scale_x Horizontal scale factor.
    /// @param scale_y Vertical scale factor.
    /// @param dx Horizontal offset, in points after scaling.
    /// @param dy Vertical offset, in points after scaling.
    /// @return The transformed copy.
    [[nodiscard]] Path transformed(float scale_x, float scale_y, float dx, float dy) const;
    /// Lines approximating the curves, no further than `tolerance` from them.
    ///
    /// @param tolerance The largest distance from the curves, in points.
    /// @return One polyline per subpath.
    [[nodiscard]] std::vector<Polyline> flatten(float tolerance = 0.25f) const;
    /// The outline of the stroke: fill it with FillRule::non_zero. Curves are flattened within
    /// `tolerance` first.
    ///
    /// @param style The line's width, caps, joins and dashes.
    /// @param tolerance The largest distance from the curves, in points.
    /// @return The stroke's outline, or an empty path when `style.width` is 0 or less.
    [[nodiscard]] Path stroked(const StrokeStyle& style, float tolerance = 0.25f) const;

    friend bool operator==(const Path&, const Path&) = default;

private:
    enum class Verb : std::uint8_t { move, line, quad, cubic, close };

    ELG_PRIVATE_MEMBERS_BEGIN
    std::vector<Verb> verbs_;
    std::vector<Point> points_; // move/line: 1, quad: 2, cubic: 3, close: 0
    ELG_PRIVATE_MEMBERS_END
    Point start_{};   // of the current subpath
    Point current_{};
    bool open_ = false; // a subpath has been started
};

} // namespace elg

#endif // ELG_PATH_H
