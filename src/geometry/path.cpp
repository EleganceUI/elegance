// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Path geometry: building, SVG path data, flattening, stroking, and hit testing (design.md §5).

#include <elg/path.h>

#include <elg/error.h>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <format>
#include <locale>
#include <numbers>
#include <sstream>
#include <string>
#include <version>

namespace elg {

namespace {

constexpr float kPi = std::numbers::pi_v<float>;
// Cubic Bézier handles for a quarter circle of radius 1.
constexpr float kKappa = 0.5522847498f;

Point operator+(Point a, Point b) {
    return {a.x + b.x, a.y + b.y};
}
Point operator-(Point a, Point b) {
    return {a.x - b.x, a.y - b.y};
}
Point operator*(Point a, float s) {
    return {a.x * s, a.y * s};
}
float dot(Point a, Point b) {
    return a.x * b.x + a.y * b.y;
}
float cross(Point a, Point b) {
    return a.x * b.y - a.y * b.x;
}
float length(Point a) {
    return std::sqrt(dot(a, a));
}
Point normalized(Point a) {
    const float l = length(a);
    return l > 0 ? a * (1 / l) : Point{};
}
// The left-hand normal (y down: points "outwards" for clockwise-on-screen contours).
Point perpendicular(Point a) {
    return {-a.y, a.x};
}

// Segments needed to keep a curve of this degree within `tolerance` (Wang's formula).
int subdivisions(float second_difference, float degree_factor, float tolerance) {
    const float n = std::sqrt(degree_factor * second_difference / std::max(tolerance, 1e-4f));
    return std::clamp(static_cast<int>(std::ceil(n)), 1, 1000);
}

} // namespace

// --- Building ---

Path& Path::move_to(Point point) {
    verbs_.push_back(Verb::move);
    points_.push_back(point);
    start_ = current_ = point;
    open_ = true;
    return *this;
}

Path& Path::line_to(Point point) {
    if (!open_) {
        move_to(current_);
    }
    verbs_.push_back(Verb::line);
    points_.push_back(point);
    current_ = point;
    return *this;
}

Path& Path::quad_to(Point control, Point end) {
    if (!open_) {
        move_to(current_);
    }
    verbs_.push_back(Verb::quad);
    points_.push_back(control);
    points_.push_back(end);
    current_ = end;
    return *this;
}

Path& Path::cubic_to(Point control1, Point control2, Point end) {
    if (!open_) {
        move_to(current_);
    }
    verbs_.push_back(Verb::cubic);
    points_.push_back(control1);
    points_.push_back(control2);
    points_.push_back(end);
    current_ = end;
    return *this;
}

// SVG's endpoint arc, converted to center form (SVG 1.1 appendix F.6), then to cubics of at most
// a quarter turn each.
Path& Path::arc_to(float rx, float ry, float rotation_degrees, bool large_arc, bool sweep, Point end) {
    const Point from = current_;
    if (from == end) {
        return *this;
    }
    rx = std::abs(rx);
    ry = std::abs(ry);
    if (rx == 0 || ry == 0) {
        return line_to(end);
    }
    const float phi = rotation_degrees * kPi / 180;
    const float cos_phi = std::cos(phi);
    const float sin_phi = std::sin(phi);
    const Point half{(from.x - end.x) / 2, (from.y - end.y) / 2};
    const Point p{cos_phi * half.x + sin_phi * half.y, -sin_phi * half.x + cos_phi * half.y};
    // Radii too small to reach: scale them up just enough.
    const float lambda = p.x * p.x / (rx * rx) + p.y * p.y / (ry * ry);
    if (lambda > 1) {
        rx *= std::sqrt(lambda);
        ry *= std::sqrt(lambda);
    }
    const float numerator = rx * rx * ry * ry - rx * rx * p.y * p.y - ry * ry * p.x * p.x;
    const float denominator = rx * rx * p.y * p.y + ry * ry * p.x * p.x;
    float factor = std::sqrt(std::max(0.f, numerator / denominator));
    if (large_arc == sweep) {
        factor = -factor;
    }
    const Point center_prime{factor * rx * p.y / ry, -factor * ry * p.x / rx};
    const Point center{cos_phi * center_prime.x - sin_phi * center_prime.y + (from.x + end.x) / 2,
                       sin_phi * center_prime.x + cos_phi * center_prime.y + (from.y + end.y) / 2};
    const auto angle = [](Point u, Point v) {
        const float a = std::atan2(cross(u, v), dot(u, v));
        return a;
    };
    const Point u{(p.x - center_prime.x) / rx, (p.y - center_prime.y) / ry};
    const Point v{(-p.x - center_prime.x) / rx, (-p.y - center_prime.y) / ry};
    const float theta = angle({1, 0}, u);
    float delta = angle(u, v);
    if (!sweep && delta > 0) {
        delta -= 2 * kPi;
    } else if (sweep && delta < 0) {
        delta += 2 * kPi;
    }

    const int pieces = std::max(1, static_cast<int>(std::ceil(std::abs(delta) / (kPi / 2) - 1e-3f)));
    const float step = delta / static_cast<float>(pieces);
    const float handle = 4.f / 3.f * std::tan(step / 4);
    const auto on_ellipse = [&](float t) {
        const Point local{rx * std::cos(t), ry * std::sin(t)};
        return Point{cos_phi * local.x - sin_phi * local.y + center.x, sin_phi * local.x + cos_phi * local.y + center.y};
    };
    const auto tangent = [&](float t) {
        const Point local{-rx * std::sin(t), ry * std::cos(t)};
        return Point{cos_phi * local.x - sin_phi * local.y, sin_phi * local.x + cos_phi * local.y};
    };
    float t = theta;
    for (int i = 0; i < pieces; ++i) {
        const float next = t + step;
        const Point a = on_ellipse(t);
        const Point b = i + 1 == pieces ? end : on_ellipse(next);
        cubic_to(a + tangent(t) * handle, b - tangent(next) * handle, b);
        t = next;
    }
    return *this;
}

Path& Path::close() {
    if (open_) {
        verbs_.push_back(Verb::close);
        current_ = start_;
        open_ = false;
    }
    return *this;
}

Path& Path::add_rect(Rect rect) {
    return move_to({rect.x, rect.y})
        .line_to({rect.max_x(), rect.y})
        .line_to({rect.max_x(), rect.max_y()})
        .line_to({rect.x, rect.max_y()})
        .close();
}

Path& Path::add_rounded_rect(Rect rect, float radius) {
    const float r = std::clamp(radius, 0.f, std::min(rect.width, rect.height) / 2);
    if (r <= 0) {
        return add_rect(rect);
    }
    const float k = r * (1 - kKappa);
    const float x0 = rect.x;
    const float y0 = rect.y;
    const float x1 = rect.max_x();
    const float y1 = rect.max_y();
    return move_to({x0 + r, y0})
        .line_to({x1 - r, y0})
        .cubic_to({x1 - k, y0}, {x1, y0 + k}, {x1, y0 + r})
        .line_to({x1, y1 - r})
        .cubic_to({x1, y1 - k}, {x1 - k, y1}, {x1 - r, y1})
        .line_to({x0 + r, y1})
        .cubic_to({x0 + k, y1}, {x0, y1 - k}, {x0, y1 - r})
        .line_to({x0, y0 + r})
        .cubic_to({x0, y0 + k}, {x0 + k, y0}, {x0 + r, y0})
        .close();
}

Path& Path::add_ellipse(Rect rect) {
    const Point c = rect.center();
    const float rx = rect.width / 2;
    const float ry = rect.height / 2;
    const float kx = rx * kKappa;
    const float ky = ry * kKappa;
    return move_to({c.x + rx, c.y})
        .cubic_to({c.x + rx, c.y + ky}, {c.x + kx, c.y + ry}, {c.x, c.y + ry})
        .cubic_to({c.x - kx, c.y + ry}, {c.x - rx, c.y + ky}, {c.x - rx, c.y})
        .cubic_to({c.x - rx, c.y - ky}, {c.x - kx, c.y - ry}, {c.x, c.y - ry})
        .cubic_to({c.x + kx, c.y - ry}, {c.x + rx, c.y - ky}, {c.x + rx, c.y})
        .close();
}

Path& Path::add_circle(Point center, float radius) {
    return add_ellipse({center.x - radius, center.y - radius, 2 * radius, 2 * radius});
}

Path& Path::add_path(const Path& other) {
    verbs_.insert(verbs_.end(), other.verbs_.begin(), other.verbs_.end());
    points_.insert(points_.end(), other.points_.begin(), other.points_.end());
    current_ = other.current_;
    start_ = other.start_;
    open_ = other.open_;
    return *this;
}

// --- SVG path data ---

namespace {

class SvgReader {
public:
    explicit SvgReader(std::string_view data) : data_(data) {}

    bool done() {
        skip_separators();
        return at_ >= data_.size();
    }
    bool command_next() {
        skip_separators();
        return at_ < data_.size() && std::isalpha(static_cast<unsigned char>(data_[at_])) && data_[at_] != 'e' &&
               data_[at_] != 'E';
    }
    char command() { return data_[at_++]; }
    bool number_next() {
        skip_separators();
        if (at_ >= data_.size()) {
            return false;
        }
        const char c = data_[at_];
        return std::isdigit(static_cast<unsigned char>(c)) || c == '-' || c == '+' || c == '.';
    }
    float number() {
        skip_separators();
        float value = 0;
        const char* begin = data_.data() + at_;
        const char* end = data_.data() + data_.size();
        if (begin < end && *begin == '+') {
            ++begin;
        }
#if defined(__cpp_lib_to_chars)
        const auto [next, error] = std::from_chars(begin, end, value);
        if (error != std::errc{}) {
            fail("expected a number");
        }
#else
        // libc++ (macOS) lacks floating-point from_chars: find the number's end the same way,
        // then parse it in the classic locale.
        const char* next = begin;
        const auto digits = [&] {
            const char* start = next;
            while (next < end && std::isdigit(static_cast<unsigned char>(*next))) {
                ++next;
            }
            return next > start;
        };
        if (next < end && *next == '-') {
            ++next;
        }
        bool mantissa = digits();
        if (next < end && *next == '.') {
            ++next;
            mantissa = digits() || mantissa;
        }
        if (!mantissa) {
            fail("expected a number");
        }
        if (next < end && (*next == 'e' || *next == 'E')) {
            const char* exponent = next++;
            if (next < end && (*next == '-' || *next == '+')) {
                ++next;
            }
            if (!digits()) {
                next = exponent; // "1e" is the number 1, then something else
            }
        }
        std::istringstream text(std::string(begin, next));
        text.imbue(std::locale::classic());
        if (!(text >> value)) {
            fail("expected a number");
        }
#endif
        at_ = static_cast<std::size_t>(next - data_.data());
        return value;
    }
    // Arc flags may be written without separators ("a1 1 0 1110 10").
    bool flag() {
        skip_separators();
        if (at_ >= data_.size() || (data_[at_] != '0' && data_[at_] != '1')) {
            fail("expected an arc flag (0 or 1)");
        }
        return data_[at_++] == '1';
    }
    Point point() {
        const float x = number();
        const float y = number();
        return {x, y};
    }
    [[noreturn]] void fail(std::string_view what) const {
        throw UsageError(std::format("Path::svg(): {} at position {} of \"{}\"", what, at_, data_));
    }

private:
    void skip_separators() {
        while (at_ < data_.size() && (std::isspace(static_cast<unsigned char>(data_[at_])) || data_[at_] == ',')) {
            ++at_;
        }
    }

    std::string_view data_;
    std::size_t at_ = 0;
};

} // namespace

Path Path::svg(std::string_view data) {
    Path path;
    SvgReader reader(data);
    char command = 0;
    Point current{};
    Point start{};
    Point last_control{}; // for S/s and T/t
    char last_command = 0;
    while (!reader.done()) {
        if (reader.command_next()) {
            command = reader.command();
        } else if (command == 0) {
            reader.fail("expected a command");
        }
        const bool relative = std::islower(static_cast<unsigned char>(command)) != 0;
        const Point base = relative ? current : Point{};
        const auto read_point = [&] { return reader.point() + base; };
        switch (std::toupper(static_cast<unsigned char>(command))) {
        case 'M':
            current = start = read_point();
            path.move_to(current);
            command = relative ? 'l' : 'L'; // further pairs are lines
            break;
        case 'L':
            current = read_point();
            path.line_to(current);
            break;
        case 'H':
            current.x = reader.number() + base.x;
            path.line_to(current);
            break;
        case 'V':
            current.y = reader.number() + base.y;
            path.line_to(current);
            break;
        case 'C': {
            const Point c1 = read_point();
            const Point c2 = read_point();
            current = read_point();
            path.cubic_to(c1, c2, current);
            last_control = c2;
            break;
        }
        case 'S': {
            const bool follows = std::toupper(static_cast<unsigned char>(last_command)) == 'C' ||
                                 std::toupper(static_cast<unsigned char>(last_command)) == 'S';
            const Point c1 = follows ? current * 2 - last_control : current;
            const Point c2 = read_point();
            current = read_point();
            path.cubic_to(c1, c2, current);
            last_control = c2;
            break;
        }
        case 'Q': {
            const Point c = read_point();
            current = read_point();
            path.quad_to(c, current);
            last_control = c;
            break;
        }
        case 'T': {
            const bool follows = std::toupper(static_cast<unsigned char>(last_command)) == 'Q' ||
                                 std::toupper(static_cast<unsigned char>(last_command)) == 'T';
            const Point c = follows ? current * 2 - last_control : current;
            current = read_point();
            path.quad_to(c, current);
            last_control = c;
            break;
        }
        case 'A': {
            const float rx = reader.number();
            const float ry = reader.number();
            const float rotation = reader.number();
            const bool large = reader.flag();
            const bool sweep = reader.flag();
            current = read_point();
            path.arc_to(rx, ry, rotation, large, sweep, current);
            break;
        }
        case 'Z':
            path.close();
            current = start;
            last_command = command;
            command = 0; // a command must follow
            continue;
        default: reader.fail(std::format("unknown command '{}'", command));
        }
        last_command = command;
        if (!reader.number_next() && !reader.done() && !reader.command_next()) {
            reader.fail("unexpected character");
        }
    }
    return path;
}

// --- Flattening ---

std::vector<Path::Polyline> Path::flatten(float tolerance) const {
    std::vector<Polyline> lines;
    Polyline* line = nullptr;
    Point current{};
    std::size_t p = 0;
    for (const Verb verb : verbs_) {
        switch (verb) {
        case Verb::move:
            current = points_[p++];
            lines.push_back({{current}, false});
            line = &lines.back();
            break;
        case Verb::line:
            current = points_[p++];
            line->points.push_back(current);
            break;
        case Verb::quad: {
            const Point c = points_[p];
            const Point e = points_[p + 1];
            p += 2;
            const int n = subdivisions(length(current - c * 2 + e), 0.25f, tolerance);
            for (int i = 1; i <= n; ++i) {
                const float t = static_cast<float>(i) / static_cast<float>(n);
                const float u = 1 - t;
                line->points.push_back(current * (u * u) + c * (2 * u * t) + e * (t * t));
            }
            current = e;
            break;
        }
        case Verb::cubic: {
            const Point c1 = points_[p];
            const Point c2 = points_[p + 1];
            const Point e = points_[p + 2];
            p += 3;
            const float dd = std::max(length(current - c1 * 2 + c2), length(c1 - c2 * 2 + e));
            const int n = subdivisions(dd, 0.75f, tolerance);
            for (int i = 1; i <= n; ++i) {
                const float t = static_cast<float>(i) / static_cast<float>(n);
                const float u = 1 - t;
                line->points.push_back(current * (u * u * u) + c1 * (3 * u * u * t) + c2 * (3 * u * t * t) +
                                       e * (t * t * t));
            }
            current = e;
            break;
        }
        case Verb::close:
            if (line) {
                line->closed = true;
                current = line->points.front();
            }
            break;
        }
    }
    // Drop repeated points (they have no direction).
    for (Polyline& polyline : lines) {
        std::vector<Point>& pts = polyline.points;
        pts.erase(std::unique(pts.begin(), pts.end(),
                              [](Point a, Point b) { return std::abs(a.x - b.x) < 1e-6f && std::abs(a.y - b.y) < 1e-6f; }),
                  pts.end());
        if (polyline.closed && pts.size() > 1 && pts.front() == pts.back()) {
            pts.pop_back();
        }
    }
    return lines;
}

Rect Path::bounds() const {
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    bool any = false;
    for (const Polyline& line : flatten(0.1f)) {
        for (const Point p : line.points) {
            if (!any) {
                x0 = x1 = p.x;
                y0 = y1 = p.y;
                any = true;
            }
            x0 = std::min(x0, p.x);
            y0 = std::min(y0, p.y);
            x1 = std::max(x1, p.x);
            y1 = std::max(y1, p.y);
        }
    }
    return {x0, y0, x1 - x0, y1 - y0};
}

bool Path::contains(Point point, FillRule rule) const {
    int winding = 0;
    for (const Polyline& line : flatten(0.1f)) {
        // Every subpath counts as closed for filling, as in SVG.
        const std::size_t n = line.points.size();
        if (n < 3) {
            continue;
        }
        for (std::size_t i = 0; i < n; ++i) {
            const Point a = line.points[i];
            const Point b = line.points[(i + 1) % n];
            if (a.y <= point.y) {
                if (b.y > point.y && cross(b - a, point - a) > 0) {
                    ++winding;
                }
            } else if (b.y <= point.y && cross(b - a, point - a) < 0) {
                --winding;
            }
        }
    }
    return rule == FillRule::non_zero ? winding != 0 : (winding % 2) != 0;
}

Path Path::transformed(float scale_x, float scale_y, float dx, float dy) const {
    Path result = *this;
    for (Point& p : result.points_) {
        p = {p.x * scale_x + dx, p.y * scale_y + dy};
    }
    result.start_ = {start_.x * scale_x + dx, start_.y * scale_y + dy};
    result.current_ = {current_.x * scale_x + dx, current_.y * scale_y + dy};
    return result;
}

// --- Stroking ---
//
// The stroke is a union of simple pieces: a quad per segment, a join piece at every corner, and
// caps at open ends, each wound the same way. Filling them with the non-zero rule is their union,
// so overlaps and self-intersections need no special handling.

namespace {

void add_polygon(Path& out, const std::vector<Point>& points) {
    if (points.size() < 3) {
        return;
    }
    // Same orientation for every piece, so non-zero filling unions them.
    float area = 0;
    for (std::size_t i = 0; i < points.size(); ++i) {
        area += cross(points[i], points[(i + 1) % points.size()]);
    }
    if (area < 0) {
        out.move_to(points.front());
        for (std::size_t i = 1; i < points.size(); ++i) {
            out.line_to(points[i]);
        }
    } else {
        out.move_to(points.back());
        for (std::size_t i = points.size() - 1; i-- > 0;) {
            out.line_to(points[i]);
        }
    }
    out.close();
}

void add_disc(Path& out, Point center, float radius, float tolerance) {
    const int n = std::clamp(static_cast<int>(std::ceil(kPi / std::acos(std::max(-1.f, 1 - tolerance / std::max(radius, 1e-3f))))), 8, 256);
    std::vector<Point> points;
    points.reserve(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        const float a = 2 * kPi * static_cast<float>(i) / static_cast<float>(n);
        points.push_back({center.x + radius * std::cos(a), center.y + radius * std::sin(a)});
    }
    add_polygon(out, points);
}

void add_join(Path& out, Point at, Point in_direction, Point out_direction, const StrokeStyle& style, float tolerance) {
    const float half = style.width / 2;
    const float turn = cross(in_direction, out_direction);
    if (std::abs(turn) < 1e-6f && dot(in_direction, out_direction) > 0) {
        return; // straight on
    }
    if (style.join == LineJoin::round) {
        add_disc(out, at, half, tolerance);
        return;
    }
    // The outer side of the corner.
    const float side = turn > 0 ? -1.f : 1.f;
    const Point a = at + perpendicular(in_direction) * (half * side);
    const Point b = at + perpendicular(out_direction) * (half * side);
    if (style.join == LineJoin::miter) {
        const float cos_half = std::sqrt(std::max(0.f, (1 + dot(in_direction, out_direction)) / 2));
        if (cos_half > 1e-4f && 1 / cos_half <= style.miter_limit) {
            const Point bisector = normalized(perpendicular(in_direction) + perpendicular(out_direction)) * side;
            add_polygon(out, {at, a, at + bisector * (half / cos_half), b});
            return;
        }
    }
    add_polygon(out, {at, a, b}); // bevel (and over-long miters)
}

void add_cap(Path& out, Point at, Point direction, const StrokeStyle& style, float tolerance) {
    const float half = style.width / 2;
    if (style.cap == LineCap::round) {
        add_disc(out, at, half, tolerance);
    } else if (style.cap == LineCap::square) {
        const Point n = perpendicular(direction) * half;
        const Point ahead = at + direction * half;
        add_polygon(out, {at + n, ahead + n, ahead - n, at - n});
    }
}

void stroke_polyline(Path& out, const std::vector<Point>& pts, bool closed, const StrokeStyle& style, float tolerance) {
    const float half = style.width / 2;
    if (pts.size() == 1) {
        // A dot: round and square caps draw it; butt draws nothing.
        if (style.cap == LineCap::round) {
            add_disc(out, pts[0], half, tolerance);
        } else if (style.cap == LineCap::square) {
            add_polygon(out, {{pts[0].x - half, pts[0].y - half}, {pts[0].x + half, pts[0].y - half},
                              {pts[0].x + half, pts[0].y + half}, {pts[0].x - half, pts[0].y + half}});
        }
        return;
    }
    const std::size_t count = closed ? pts.size() : pts.size() - 1;
    for (std::size_t i = 0; i < count; ++i) {
        const Point a = pts[i];
        const Point b = pts[(i + 1) % pts.size()];
        const Point d = normalized(b - a);
        const Point n = perpendicular(d) * half;
        add_polygon(out, {a + n, b + n, b - n, a - n});
    }
    const auto direction = [&](std::size_t i) { return normalized(pts[(i + 1) % pts.size()] - pts[i]); };
    for (std::size_t i = closed ? 0 : 1; i < (closed ? pts.size() : pts.size() - 1); ++i) {
        const std::size_t previous = (i + pts.size() - 1) % pts.size();
        add_join(out, pts[i], direction(previous), direction(i), style, tolerance);
    }
    if (!closed) {
        add_cap(out, pts.front(), normalized(pts[0] - pts[1]), style, tolerance);
        add_cap(out, pts.back(), normalized(pts.back() - pts[pts.size() - 2]), style, tolerance);
    }
}

// Splits a polyline into the "on" stretches of a dash pattern.
std::vector<std::vector<Point>> dashed(const std::vector<Point>& pts, bool closed, const StrokeStyle& style) {
    float total = 0;
    for (const float d : style.dash) {
        total += std::max(d, 0.f);
    }
    std::vector<std::vector<Point>> pieces;
    if (total <= 0) {
        pieces.push_back(pts);
        if (closed) {
            pieces.back().push_back(pts.front());
        }
        return pieces;
    }
    std::size_t index = 0;
    float remaining = std::max(style.dash[0], 0.f);
    bool on = true;
    // Start `dash_phase` into the pattern.
    float phase = std::fmod(style.dash_phase, total);
    if (phase < 0) {
        phase += total;
    }
    while (phase > 0) {
        const float step = std::min(phase, remaining);
        phase -= step;
        remaining -= step;
        if (remaining <= 0) {
            index = (index + 1) % style.dash.size();
            remaining = std::max(style.dash[index], 0.f);
            on = index % 2 == 0;
        }
    }
    std::vector<Point> current;
    if (on) {
        current.push_back(pts.front());
    }
    const std::size_t segments = closed ? pts.size() : pts.size() - 1;
    for (std::size_t i = 0; i < segments; ++i) {
        Point a = pts[i];
        const Point b = pts[(i + 1) % pts.size()];
        float left = length(b - a);
        const Point d = normalized(b - a);
        while (left > 0) {
            const float step = std::min(left, remaining);
            a = a + d * step;
            left -= step;
            remaining -= step;
            if (on) {
                current.push_back(a);
            }
            if (remaining <= 1e-6f) {
                if (on && current.size() > 1) {
                    pieces.push_back(std::move(current));
                }
                current.clear();
                index = (index + 1) % style.dash.size();
                remaining = std::max(style.dash[index], 0.f);
                on = index % 2 == 0;
                if (on) {
                    current.push_back(a);
                }
            }
        }
    }
    if (on && current.size() > 1) {
        pieces.push_back(std::move(current));
    }
    return pieces;
}

} // namespace

Path Path::stroked(const StrokeStyle& style, float tolerance) const {
    Path out;
    if (style.width <= 0) {
        return out;
    }
    for (const Polyline& line : flatten(tolerance)) {
        if (line.points.empty()) {
            continue;
        }
        if (style.dash.empty()) {
            stroke_polyline(out, line.points, line.closed && line.points.size() > 2, style, tolerance);
        } else {
            for (const std::vector<Point>& piece : dashed(line.points, line.closed, style)) {
                stroke_polyline(out, piece, false, style, tolerance);
            }
        }
    }
    return out;
}

} // namespace elg
