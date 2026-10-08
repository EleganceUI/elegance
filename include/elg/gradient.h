// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_GRADIENT_H
#define ELG_GRADIENT_H

#include <elg/color.h>

#include <initializer_list>
#include <utility>
#include <variant>
#include <vector>

namespace elg {

// A point in a view's bounds, in unit coordinates: (0, 0) top-leading, (1, 1) bottom-trailing.
struct UnitPoint {
    float x = 0.5f;
    float y = 0.5f;

    static const UnitPoint center;
    static const UnitPoint top;
    static const UnitPoint bottom;
    static const UnitPoint leading;
    static const UnitPoint trailing;
    static const UnitPoint top_leading;
    static const UnitPoint top_trailing;
    static const UnitPoint bottom_leading;
    static const UnitPoint bottom_trailing;
};

inline constexpr UnitPoint UnitPoint::center{0.5f, 0.5f};
inline constexpr UnitPoint UnitPoint::top{0.5f, 0};
inline constexpr UnitPoint UnitPoint::bottom{0.5f, 1};
inline constexpr UnitPoint UnitPoint::leading{0, 0.5f};
inline constexpr UnitPoint UnitPoint::trailing{1, 0.5f};
inline constexpr UnitPoint UnitPoint::top_leading{0, 0};
inline constexpr UnitPoint UnitPoint::top_trailing{1, 0};
inline constexpr UnitPoint UnitPoint::bottom_leading{0, 1};
inline constexpr UnitPoint UnitPoint::bottom_trailing{1, 1};

struct GradientStop {
    Color color;
    float location = 0; // 0...1 along the gradient
};

namespace detail {
inline std::vector<GradientStop> even_stops(std::initializer_list<Color> colors) {
    std::vector<GradientStop> stops;
    const std::size_t n = colors.size();
    std::size_t i = 0;
    for (const Color& color : colors) {
        stops.push_back({color, n > 1 ? static_cast<float>(i++) / static_cast<float>(n - 1) : 0.f});
    }
    return stops;
}
} // namespace detail

// Colors blend along the line from `start` to `end` (unit points of the filled shape's bounds).
// Stops interpolate in sRGB with premultiplied alpha, as in CSS.
struct LinearGradient {
    std::vector<GradientStop> stops;
    UnitPoint start = UnitPoint::top;
    UnitPoint end = UnitPoint::bottom;

    LinearGradient(std::vector<GradientStop> stops, UnitPoint start = UnitPoint::top,
                   UnitPoint end = UnitPoint::bottom)
        : stops(std::move(stops)), start(start), end(end) {}
    // Evenly spaced colors: LinearGradient({Color::hex(0x6E8BFF), Color::hex(0xA36BFF)})
    LinearGradient(std::initializer_list<Color> colors, UnitPoint start = UnitPoint::top,
                   UnitPoint end = UnitPoint::bottom)
        : stops(detail::even_stops(colors)), start(start), end(end) {}
};

// Colors blend outward from `center` between two radii in points.
struct RadialGradient {
    std::vector<GradientStop> stops;
    UnitPoint center = UnitPoint::center;
    float start_radius = 0;
    float end_radius = 100;

    RadialGradient(std::vector<GradientStop> stops, UnitPoint center = UnitPoint::center,
                   float start_radius = 0, float end_radius = 100)
        : stops(std::move(stops)), center(center), start_radius(start_radius), end_radius(end_radius) {}
    RadialGradient(std::initializer_list<Color> colors, UnitPoint center = UnitPoint::center,
                   float start_radius = 0, float end_radius = 100)
        : stops(detail::even_stops(colors)), center(center), start_radius(start_radius),
          end_radius(end_radius) {}
};

// What a shape or background is filled with: a color or a gradient. Converts implicitly, so
// .fill(Color::red) and .fill(LinearGradient({...})) both work.
class ShapeStyle {
public:
    using Value = std::variant<Color, LinearGradient, RadialGradient>;

    ShapeStyle(Color color) : value_(color) {}
    ShapeStyle(LinearGradient gradient) : value_(std::move(gradient)) {}
    ShapeStyle(RadialGradient gradient) : value_(std::move(gradient)) {}

    const Value& value() const noexcept { return value_; }

private:
    Value value_;
};

} // namespace elg

#endif // ELG_GRADIENT_H
