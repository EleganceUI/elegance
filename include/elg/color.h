// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_COLOR_H
#define ELG_COLOR_H

#include <cstdint>

namespace elg {

/// An sRGB color with straight (non-premultiplied) alpha; components are in [0, 1]. The default is opaque
/// black.
struct Color {
    float r = 0; ///< Red.
    float g = 0; ///< Green.
    float b = 0; ///< Blue.
    float a = 1; ///< Alpha (opacity): 0 is transparent, 1 is opaque.

    /// A color from 8-bit sRGB components, e.g. `Color::rgb(0, 122, 255)`.
    ///
    /// @param red Red, 0 to 255.
    /// @param green Green, 0 to 255.
    /// @param blue Blue, 0 to 255.
    /// @param alpha Opacity in [0, 1].
    /// @return The color, with each component divided by 255.
    static constexpr Color rgb(std::uint8_t red, std::uint8_t green, std::uint8_t blue,
                               float alpha = 1) noexcept {
        return {static_cast<float>(red) / 255.f, static_cast<float>(green) / 255.f, static_cast<float>(blue) / 255.f,
                alpha};
    }

    /// A color from a hex literal, e.g. `Color::hex(0x007AFF)`.
    ///
    /// @param rrggbb The color as 0xRRGGBB; bits above the low 24 are ignored.
    /// @param alpha Opacity in [0, 1].
    /// @return The color.
    static constexpr Color hex(std::uint32_t rrggbb, float alpha = 1) noexcept {
        return rgb(static_cast<std::uint8_t>(rrggbb >> 16), static_cast<std::uint8_t>(rrggbb >> 8),
                   static_cast<std::uint8_t>(rrggbb), alpha);
    }

    /// Multiplies the alpha, e.g. `Color::black.opacity(0.5f)`.
    ///
    /// @param alpha The factor to multiply `a` by.
    /// @return This color with its alpha multiplied by `alpha`.
    [[nodiscard]] constexpr Color opacity(float alpha) const noexcept { return {r, g, b, a * alpha}; }

    /// Blends toward `other` (in sRGB), alpha included.
    ///
    /// @param other The color to blend toward.
    /// @param amount 0 is this color, 1 is `other`. Not clamped: values outside [0, 1] extrapolate.
    /// @return The blended color.
    [[nodiscard]] constexpr Color mix(Color other, float amount) const noexcept {
        return {r + (other.r - r) * amount, g + (other.g - g) * amount, b + (other.b - b) * amount,
                a + (other.a - a) * amount};
    }

    friend constexpr bool operator==(const Color&, const Color&) = default;

    static const Color black; ///< Opaque black.
    static const Color white; ///< Opaque white.
    static const Color clear; ///< Fully transparent black.
};

inline constexpr Color Color::black{0, 0, 0, 1};
inline constexpr Color Color::white{1, 1, 1, 1};
inline constexpr Color Color::clear{0, 0, 0, 0};

} // namespace elg

#endif // ELG_COLOR_H
