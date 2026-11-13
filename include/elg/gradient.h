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

/// A point in a view's bounds, in unit coordinates: (0, 0) top-leading, (1, 1) bottom-trailing.
/// Coordinates outside 0...1 are outside the bounds.
struct UnitPoint {
    float x = 0.5f; ///< 0 at the leading edge, 1 at the trailing edge
    float y = 0.5f; ///< 0 at the top, 1 at the bottom

    static const UnitPoint center;          ///< (0.5, 0.5)
    static const UnitPoint top;             ///< (0.5, 0)
    static const UnitPoint bottom;          ///< (0.5, 1)
    static const UnitPoint leading;         ///< (0, 0.5)
    static const UnitPoint trailing;        ///< (1, 0.5)
    static const UnitPoint top_leading;     ///< (0, 0)
    static const UnitPoint top_trailing;    ///< (1, 0)
    static const UnitPoint bottom_leading;  ///< (0, 1)
    static const UnitPoint bottom_trailing; ///< (1, 1)
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

/// A color at a point along a gradient. Stops needn't be in order (they're sorted by location
/// when drawn); before the first stop and past the last, the end colors continue.
struct GradientStop {
    Color color;        ///< the color at `location`
    float location = 0; ///< 0...1 along the gradient
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

/// Colors blend along the line from `start` to `end` (unit points of the filled shape's bounds).
/// Stops interpolate in sRGB with premultiplied alpha, as in CSS.
struct LinearGradient {
    std::vector<GradientStop> stops;   ///< locations along the line: 0 at `start`, 1 at `end`
    UnitPoint start = UnitPoint::top;  ///< where location 0 is
    UnitPoint end = UnitPoint::bottom; ///< where location 1 is

    /// A gradient through `color_stops`, from top to bottom by default.
    ///
    /// @param color_stops The colors and their locations.
    /// @param from Where location 0 is.
    /// @param to Where location 1 is.
    LinearGradient(std::vector<GradientStop> color_stops, UnitPoint from = UnitPoint::top,
                   UnitPoint to = UnitPoint::bottom)
        : stops(std::move(color_stops)), start(from), end(to) {}
    /// Like the constructor from stops, but with evenly spaced colors:
    ///
    /// @code
    /// LinearGradient({Color::hex(0x6E8BFF), Color::hex(0xA36BFF)})
    /// @endcode
    ///
    /// @param colors The colors, the first at location 0 and the last at 1.
    /// @param from Where location 0 is.
    /// @param to Where location 1 is.
    LinearGradient(std::initializer_list<Color> colors, UnitPoint from = UnitPoint::top,
                   UnitPoint to = UnitPoint::bottom)
        : stops(detail::even_stops(colors)), start(from), end(to) {}
};

/// Colors blend outward from `center` between two radii in points.
struct RadialGradient {
    std::vector<GradientStop> stops;      ///< locations between the radii: 0 at `start_radius`, 1 at `end_radius`
    UnitPoint center = UnitPoint::center; ///< the circles' center
    float start_radius = 0;               ///< where location 0 is, in points from `center`
    float end_radius = 100;               ///< where location 1 is, in points from `center`

    /// A gradient through `color_stops`, from the center out to 100 points by default.
    ///
    /// @param color_stops The colors and their locations.
    /// @param at The circles' center.
    /// @param from_radius Where location 0 is, in points.
    /// @param to_radius Where location 1 is, in points.
    RadialGradient(std::vector<GradientStop> color_stops, UnitPoint at = UnitPoint::center,
                   float from_radius = 0, float to_radius = 100)
        : stops(std::move(color_stops)), center(at), start_radius(from_radius), end_radius(to_radius) {}
    /// Like the constructor from stops, but with evenly spaced colors.
    ///
    /// @param colors The colors, the first at `from_radius` and the last at `to_radius`.
    /// @param at The circles' center.
    /// @param from_radius Where the first color is, in points.
    /// @param to_radius Where the last color is, in points.
    RadialGradient(std::initializer_list<Color> colors, UnitPoint at = UnitPoint::center,
                   float from_radius = 0, float to_radius = 100)
        : stops(detail::even_stops(colors)), center(at), start_radius(from_radius), end_radius(to_radius) {}
};

/// Colors blend outward from `center` along ellipses shaped like the filled bounds. The radii are
/// fractions of the bounds' size: 0.5 reaches the middle of each edge.
struct EllipticalGradient {
    std::vector<GradientStop> stops;      ///< locations between the radii: 0 at the start one, 1 at the end one
    UnitPoint center = UnitPoint::center; ///< the ellipses' center
    float start_radius_fraction = 0;      ///< where location 0 is
    float end_radius_fraction = 0.5f;     ///< where location 1 is

    /// A gradient through `color_stops`, from the center out to the middle of each edge by default.
    ///
    /// @param color_stops The colors and their locations.
    /// @param at The ellipses' center.
    /// @param from_fraction Where location 0 is, as a fraction of the bounds' size.
    /// @param to_fraction Where location 1 is, as a fraction of the bounds' size.
    EllipticalGradient(std::vector<GradientStop> color_stops, UnitPoint at = UnitPoint::center,
                       float from_fraction = 0, float to_fraction = 0.5f)
        : stops(std::move(color_stops)), center(at), start_radius_fraction(from_fraction),
          end_radius_fraction(to_fraction) {}
    /// Like the constructor from stops, but with evenly spaced colors.
    ///
    /// @param colors The colors, the first at `from_fraction` and the last at `to_fraction`.
    /// @param at The ellipses' center.
    /// @param from_fraction Where the first color is, as a fraction of the bounds' size.
    /// @param to_fraction Where the last color is, as a fraction of the bounds' size.
    EllipticalGradient(std::initializer_list<Color> colors, UnitPoint at = UnitPoint::center,
                       float from_fraction = 0, float to_fraction = 0.5f)
        : stops(detail::even_stops(colors)), center(at), start_radius_fraction(from_fraction),
          end_radius_fraction(to_fraction) {}
};

/// Colors sweep around `center` (a conic gradient), clockwise from `start_angle` to `end_angle`,
/// in degrees; 0 points trailing (right), 90 down. A full turn by default. Short of a full turn,
/// the last color continues from `end_angle` round to `start_angle`.
struct AngularGradient {
    std::vector<GradientStop> stops;      ///< locations between the angles: 0 at `start_angle`, 1 at `end_angle`
    UnitPoint center = UnitPoint::center; ///< what the colors sweep around
    float start_angle = 0;                ///< where location 0 is, in degrees
    float end_angle = 360;                ///< where location 1 is, in degrees; greater than `start_angle`

    /// A gradient through `color_stops`, a full turn by default.
    ///
    /// @param color_stops The colors and their locations.
    /// @param at What the colors sweep around.
    /// @param from_angle Where location 0 is, in degrees clockwise from trailing.
    /// @param to_angle Where location 1 is, in degrees; greater than `from_angle`.
    AngularGradient(std::vector<GradientStop> color_stops, UnitPoint at = UnitPoint::center, float from_angle = 0,
                    float to_angle = 360)
        : stops(std::move(color_stops)), center(at), start_angle(from_angle), end_angle(to_angle) {}
    /// Like the constructor from stops, but with evenly spaced colors.
    ///
    /// @param colors The colors, the first at `from_angle` and the last at `to_angle`.
    /// @param at What the colors sweep around.
    /// @param from_angle Where the first color is, in degrees clockwise from trailing.
    /// @param to_angle Where the last color is, in degrees; greater than `from_angle`.
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

/// What a shape or background is filled with: a color or a gradient. Converts implicitly, so
/// .fill(Color::red) and .fill(LinearGradient({...})) both work. (.backdrop_blur() fills with a
/// detail::BackdropFill.)
class ShapeStyle {
public:
    /// One of the fills a ShapeStyle can hold.
    using Value = std::variant<Color, LinearGradient, RadialGradient, EllipticalGradient, AngularGradient, detail::BackdropFill>;

    /// A solid fill.
    ///
    /// @param color The color.
    ShapeStyle(Color color) : value_(color) {}
    /// A linear gradient fill.
    ///
    /// @param gradient The gradient.
    ShapeStyle(LinearGradient gradient) : value_(std::move(gradient)) {}
    /// A radial gradient fill.
    ///
    /// @param gradient The gradient.
    ShapeStyle(RadialGradient gradient) : value_(std::move(gradient)) {}
    /// An elliptical gradient fill.
    ///
    /// @param gradient The gradient.
    ShapeStyle(EllipticalGradient gradient) : value_(std::move(gradient)) {}
    /// An angular (conic) gradient fill.
    ///
    /// @param gradient The gradient.
    ShapeStyle(AngularGradient gradient) : value_(std::move(gradient)) {}
    /// What .backdrop_blur() fills with: the window behind, blurred.
    ///
    /// @param backdrop The blur and color adjustments.
    ShapeStyle(detail::BackdropFill backdrop) : value_(backdrop) {}

    /// The fill, for code that draws it.
    ///
    /// @return The color or gradient (or backdrop), as one alternative of Value.
    [[nodiscard]] const Value& value() const noexcept { return value_; }

private:
    Value value_;
};

} // namespace elg

#endif // ELG_GRADIENT_H
