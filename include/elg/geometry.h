// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_GEOMETRY_H
#define ELG_GEOMETRY_H

#include <algorithm>
#include <cstdint>

namespace elg {

// Physical render dimensions, unlike the logical-point geometry below.
struct PixelSize {
    std::uint32_t width = 0;
    std::uint32_t height = 0;

    friend constexpr bool operator==(const PixelSize&, const PixelSize&) = default;
};

// All remaining geometry is in logical points; the renderer applies the window's display scale.

struct Point {
    float x = 0;
    float y = 0;

    friend constexpr bool operator==(const Point&, const Point&) = default;
};

struct Size {
    float width = 0;
    float height = 0;
};

struct Rect {
    float x = 0;
    float y = 0;
    float width = 0;
    float height = 0;

    constexpr float min_x() const noexcept { return x; }
    constexpr float min_y() const noexcept { return y; }
    constexpr float max_x() const noexcept { return x + width; }
    constexpr float max_y() const noexcept { return y + height; }
    constexpr Point center() const noexcept { return {x + width * 0.5f, y + height * 0.5f}; }
    constexpr Size size() const noexcept { return {width, height}; }

    // Shrinks the rect by `amount` on every side (grows it when negative), never below zero size.
    constexpr Rect inset(float amount) const noexcept {
        return {x + amount, y + amount, std::max(0.f, width - 2 * amount),
                std::max(0.f, height - 2 * amount)};
    }

    friend constexpr bool operator==(const Rect&, const Rect&) = default;
};

struct CornerRadii {
    float top_left = 0;
    float top_right = 0;
    float bottom_right = 0;
    float bottom_left = 0;

    static constexpr CornerRadii all(float radius) noexcept {
        return {radius, radius, radius, radius};
    }
};

} // namespace elg

#endif // ELG_GEOMETRY_H
