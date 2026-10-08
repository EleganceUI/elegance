// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_COLOR_H
#define ELG_COLOR_H

#include <cstdint>

namespace elg {

// An sRGB color with straight (non-premultiplied) alpha; components are in [0, 1].
struct Color {
    float r = 0;
    float g = 0;
    float b = 0;
    float a = 1;

    static constexpr Color rgb(std::uint8_t red, std::uint8_t green, std::uint8_t blue,
                               float alpha = 1) noexcept {
        return {red / 255.f, green / 255.f, blue / 255.f, alpha};
    }

    // From 0xRRGGBB.
    static constexpr Color hex(std::uint32_t rrggbb, float alpha = 1) noexcept {
        return rgb(static_cast<std::uint8_t>(rrggbb >> 16), static_cast<std::uint8_t>(rrggbb >> 8),
                   static_cast<std::uint8_t>(rrggbb), alpha);
    }

    // Multiplies the alpha, e.g. Color::black.opacity(0.5f).
    constexpr Color opacity(float alpha) const noexcept { return {r, g, b, a * alpha}; }

    // Blends toward `other` (in sRGB): 0 is this color, 1 is `other`.
    constexpr Color mix(Color other, float amount) const noexcept {
        return {r + (other.r - r) * amount, g + (other.g - g) * amount, b + (other.b - b) * amount,
                a + (other.a - a) * amount};
    }

    static const Color black;
    static const Color white;
    static const Color clear;
};

inline constexpr Color Color::black{0, 0, 0, 1};
inline constexpr Color Color::white{1, 1, 1, 1};
inline constexpr Color Color::clear{0, 0, 0, 0};

} // namespace elg

#endif // ELG_COLOR_H
