// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_GRADIENT_H
#define ELG_GRADIENT_H

#include <elg/color.h>

#include <initializer_list>
#include <optional>
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

    LinearGradient(std::vector<GradientStop> color_stops, UnitPoint from = UnitPoint::top,
                   UnitPoint to = UnitPoint::bottom)
        : stops(std::move(color_stops)), start(from), end(to) {}
    // Evenly spaced colors: LinearGradient({Color::hex(0x6E8BFF), Color::hex(0xA36BFF)})
    LinearGradient(std::initializer_list<Color> colors, UnitPoint from = UnitPoint::top,
                   UnitPoint to = UnitPoint::bottom)
        : stops(detail::even_stops(colors)), start(from), end(to) {}
};

// Colors blend outward from `center` between two radii in points.
struct RadialGradient {
    std::vector<GradientStop> stops;
    UnitPoint center = UnitPoint::center;
    float start_radius = 0;
    float end_radius = 100;

    RadialGradient(std::vector<GradientStop> color_stops, UnitPoint at = UnitPoint::center,
                   float from_radius = 0, float to_radius = 100)
        : stops(std::move(color_stops)), center(at), start_radius(from_radius), end_radius(to_radius) {}
    RadialGradient(std::initializer_list<Color> colors, UnitPoint at = UnitPoint::center,
                   float from_radius = 0, float to_radius = 100)
        : stops(detail::even_stops(colors)), center(at), start_radius(from_radius), end_radius(to_radius) {}
};

// Colors blend outward from `center` along ellipses shaped like the filled bounds. The radii are
// fractions of the bounds' size: 0.5 reaches the middle of each edge.
struct EllipticalGradient {
    std::vector<GradientStop> stops;
    UnitPoint center = UnitPoint::center;
    float start_radius_fraction = 0;
    float end_radius_fraction = 0.5f;

    EllipticalGradient(std::vector<GradientStop> color_stops, UnitPoint at = UnitPoint::center,
                       float from_fraction = 0, float to_fraction = 0.5f)
        : stops(std::move(color_stops)), center(at), start_radius_fraction(from_fraction),
          end_radius_fraction(to_fraction) {}
    EllipticalGradient(std::initializer_list<Color> colors, UnitPoint at = UnitPoint::center,
                       float from_fraction = 0, float to_fraction = 0.5f)
        : stops(detail::even_stops(colors)), center(at), start_radius_fraction(from_fraction),
          end_radius_fraction(to_fraction) {}
};

// Colors sweep around `center` (a conic gradient), clockwise from `start_angle` to `end_angle`,
// in degrees; 0 points trailing (right), 90 down. A full turn by default.
struct AngularGradient {
    std::vector<GradientStop> stops;
    UnitPoint center = UnitPoint::center;
    float start_angle = 0;
    float end_angle = 360;

    AngularGradient(std::vector<GradientStop> color_stops, UnitPoint at = UnitPoint::center, float from_angle = 0,
                    float to_angle = 360)
        : stops(std::move(color_stops)), center(at), start_angle(from_angle), end_angle(to_angle) {}
    AngularGradient(std::initializer_list<Color> colors, UnitPoint at = UnitPoint::center,
                    float from_angle = 0, float to_angle = 360)
        : stops(detail::even_stops(colors)), center(at), start_angle(from_angle), end_angle(to_angle) {}
};

namespace detail {
// What .backdrop_blur() fills its shape with: what's drawn behind it in the window, blurred, then
// adjusted. Inside an offscreen group (opacity below 1, a shadow, a blur, ...) it sees only what
// the group drew behind it, as CSS's backdrop-filter does.
struct BackdropFill {
    float blur = 0;       // radius in points
    float saturation = 1; // 1 keeps the colors
    float brightness = 0; // added to each channel: -1 black ... 1 white
    float noise = 0;      // grain, 0...1, so smooth gradients don't band
};
} // namespace detail

// What a shape or background is filled with: a color or a gradient. Converts implicitly, so
// .fill(Color::red) and .fill(LinearGradient({...})) both work. (.backdrop_blur() fills with a
// detail::BackdropFill.)
class ShapeStyle {
public:
    using Value = std::variant<Color, LinearGradient, RadialGradient, EllipticalGradient, AngularGradient, detail::BackdropFill>;

    ShapeStyle(Color color) : value_(color) {}
    ShapeStyle(LinearGradient gradient) : value_(std::move(gradient)) {}
    ShapeStyle(RadialGradient gradient) : value_(std::move(gradient)) {}
    ShapeStyle(EllipticalGradient gradient) : value_(std::move(gradient)) {}
    ShapeStyle(AngularGradient gradient) : value_(std::move(gradient)) {}
    ShapeStyle(detail::BackdropFill backdrop) : value_(backdrop) {}

    const Value& value() const noexcept { return value_; }

private:
    Value value_;
};

} // namespace elg

#endif // ELG_GRADIENT_H
