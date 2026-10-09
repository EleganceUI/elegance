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

// Which points a path's outline encloses where it overlaps itself.
enum class FillRule {
    non_zero, // inside if the outline winds around the point at all (overlaps stay filled)
    even_odd, // inside if a ray from the point crosses the outline an odd number of times (holes)
};

enum class LineCap { butt, round, square };
enum class LineJoin { miter, round, bevel };

struct StrokeStyle {
    float width = 1;
    LineCap cap = LineCap::butt;
    LineJoin join = LineJoin::miter;
    float miter_limit = 4;    // miter joins longer than this many widths become bevels
    std::vector<float> dash;  // on, off, on, off... lengths; empty: solid
    float dash_phase = 0;     // how far into the pattern the stroke starts
};

class ELG_API Path {
public:
    // One flattened subpath: points joined by straight lines.
    struct Polyline {
        std::vector<Point> points;
        bool closed = false;
    };

    Path() = default;

    // --- Building (each returns *this) ---

    Path& move_to(Point point);
    Path& line_to(Point point);
    Path& quad_to(Point control, Point end);
    Path& cubic_to(Point control1, Point control2, Point end);
    // An elliptical arc to `end`, as in SVG (rx, ry, x-axis rotation in degrees, flags).
    Path& arc_to(float rx, float ry, float rotation_degrees, bool large_arc, bool sweep, Point end);
    Path& close();

    Path& add_rect(Rect rect);
    Path& add_rounded_rect(Rect rect, float radius);
    Path& add_ellipse(Rect rect);
    Path& add_circle(Point center, float radius);
    Path& add_path(const Path& other);

    // Parses SVG path data ("M10 10 L20 20 Z", all commands, relative and absolute). Throws
    // elg::UsageError with the position of anything it can't read.
    static Path svg(std::string_view data);

    // --- Queries ---

    bool empty() const noexcept { return verbs_.empty(); }
    // The smallest rect around the path's outline (curves included, not their control points).
    Rect bounds() const;
    bool contains(Point point, FillRule rule = FillRule::non_zero) const;

    // Scaled, then moved: (x * scale_x + dx, y * scale_y + dy).
    Path transformed(float scale_x, float scale_y, float dx, float dy) const;
    // Lines approximating the curves, no further than `tolerance` from them.
    std::vector<Polyline> flatten(float tolerance = 0.25f) const;
    // The outline of the stroke: fill it with FillRule::non_zero. Curves are flattened within
    // `tolerance` first.
    Path stroked(const StrokeStyle& style, float tolerance = 0.25f) const;

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
