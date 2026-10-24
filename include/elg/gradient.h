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

// Colors blend outward from `center` along ellipses shaped like the filled bounds. The radii are
// fractions of the bounds' size: 0.5 reaches the middle of each edge.
struct EllipticalGradient {
    std::vector<GradientStop> stops;
    UnitPoint center = UnitPoint::center;
    float start_radius_fraction = 0;
    float end_radius_fraction = 0.5f;

    EllipticalGradient(std::vector<GradientStop> stops, UnitPoint center = UnitPoint::center,
                       float start_radius_fraction = 0, float end_radius_fraction = 0.5f)
        : stops(std::move(stops)), center(center), start_radius_fraction(start_radius_fraction),
          end_radius_fraction(end_radius_fraction) {}
    EllipticalGradient(std::initializer_list<Color> colors, UnitPoint center = UnitPoint::center,
                       float start_radius_fraction = 0, float end_radius_fraction = 0.5f)
        : stops(detail::even_stops(colors)), center(center), start_radius_fraction(start_radius_fraction),
          end_radius_fraction(end_radius_fraction) {}
};

// Colors sweep around `center` (a conic gradient), clockwise from `start_angle` to `end_angle`,
// in degrees; 0 points trailing (right), 90 down. A full turn by default.
struct AngularGradient {
    std::vector<GradientStop> stops;
    UnitPoint center = UnitPoint::center;
    float start_angle = 0;
    float end_angle = 360;

    AngularGradient(std::vector<GradientStop> stops, UnitPoint center = UnitPoint::center, float start_angle = 0,
                    float end_angle = 360)
        : stops(std::move(stops)), center(center), start_angle(start_angle), end_angle(end_angle) {}
    AngularGradient(std::initializer_list<Color> colors, UnitPoint center = UnitPoint::center,
                    float start_angle = 0, float end_angle = 360)
        : stops(detail::even_stops(colors)), center(center), start_angle(start_angle), end_angle(end_angle) {}
};

// Frosted glass: the shape shows what's drawn behind it in the window, blurred, with its colors
// boosted, under a translucent tint and a little grain. The presets follow the color scheme
// (a light tint in light mode, a dark one in dark mode):
//
//   Text("Now playing").padding(12).background(Material::regular, RoundedRectangle(12))
//   Rectangle().fill(Material::thin)
//
// Inside an offscreen group (opacity below 1, a shadow, a blur, ...) a material sees only what
// the group drew behind it, as CSS's backdrop-filter does.
struct Material {
    float blur = 30;           // radius in points
    float saturation = 1.8f;   // of what shows through; 1 keeps the colors
    Color tint = Color{0.97f, 0.97f, 0.98f, 0.55f};
    // The tint in dark mode; unset uses `tint` in both.
    std::optional<Color> dark_tint = Color{0.11f, 0.11f, 0.12f, 0.6f};
    float noise = 0.02f;       // grain, 0...1, so smooth gradients don't band

    static const Material ultra_thin;
    static const Material thin;
    static const Material regular;
    static const Material thick;
    static const Material ultra_thick;
};

inline constexpr Material Material::ultra_thin{24, 1.8f, {0.97f, 0.97f, 0.98f, 0.2f}, Color{0.11f, 0.11f, 0.12f, 0.3f}, 0.02f};
inline constexpr Material Material::thin{28, 1.8f, {0.97f, 0.97f, 0.98f, 0.38f}, Color{0.11f, 0.11f, 0.12f, 0.45f}, 0.02f};
inline constexpr Material Material::regular{30, 1.8f, {0.97f, 0.97f, 0.98f, 0.55f}, Color{0.11f, 0.11f, 0.12f, 0.6f}, 0.02f};
inline constexpr Material Material::thick{36, 1.8f, {0.97f, 0.97f, 0.98f, 0.72f}, Color{0.11f, 0.11f, 0.12f, 0.74f}, 0.02f};
inline constexpr Material Material::ultra_thick{40, 1.8f, {0.97f, 0.97f, 0.98f, 0.86f}, Color{0.11f, 0.11f, 0.12f, 0.86f}, 0.02f};

// What a shape or background is filled with: a color, a gradient, or a material. Converts
// implicitly, so .fill(Color::red), .fill(LinearGradient({...})) and .fill(Material::thin) all work.
class ShapeStyle {
public:
    using Value = std::variant<Color, LinearGradient, RadialGradient, EllipticalGradient, AngularGradient, Material>;

    ShapeStyle(Color color) : value_(color) {}
    ShapeStyle(LinearGradient gradient) : value_(std::move(gradient)) {}
    ShapeStyle(RadialGradient gradient) : value_(std::move(gradient)) {}
    ShapeStyle(EllipticalGradient gradient) : value_(std::move(gradient)) {}
    ShapeStyle(AngularGradient gradient) : value_(std::move(gradient)) {}
    ShapeStyle(Material material) : value_(material) {}

    const Value& value() const noexcept { return value_; }

private:
    Value value_;
};

} // namespace elg

#endif // ELG_GRADIENT_H
