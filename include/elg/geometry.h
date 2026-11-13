// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_GEOMETRY_H
#define ELG_GEOMETRY_H

#include <algorithm>
#include <cstdint>

namespace elg {

/// Physical render dimensions, unlike the logical-point geometry below.
struct PixelSize {
    std::uint32_t width = 0;  ///< Width in pixels.
    std::uint32_t height = 0; ///< Height in pixels.

    friend constexpr bool operator==(const PixelSize&, const PixelSize&) = default;
};

// All remaining geometry is in logical points; the renderer applies the window's display scale.

/// A position in points. y grows downward.
struct Point {
    float x = 0; ///< Horizontal position.
    float y = 0; ///< Vertical position, growing downward.

    friend constexpr bool operator==(const Point&, const Point&) = default;
};

/// A width and height in points.
struct Size {
    float width = 0;  ///< Width.
    float height = 0; ///< Height.
};

/// An axis-aligned rectangle in points: its top-left corner and its size.
struct Rect {
    float x = 0;      ///< Left edge.
    float y = 0;      ///< Top edge.
    float width = 0;  ///< Width.
    float height = 0; ///< Height.

    /// @return The left edge, `x`.
    [[nodiscard]] constexpr float min_x() const noexcept { return x; }
    /// @return The top edge, `y`.
    [[nodiscard]] constexpr float min_y() const noexcept { return y; }
    /// @return The right edge, `x + width`.
    [[nodiscard]] constexpr float max_x() const noexcept { return x + width; }
    /// @return The bottom edge, `y + height`.
    [[nodiscard]] constexpr float max_y() const noexcept { return y + height; }
    /// @return The point in the middle of the rect.
    [[nodiscard]] constexpr Point center() const noexcept { return {x + width * 0.5f, y + height * 0.5f}; }
    /// @return The rect's width and height.
    [[nodiscard]] constexpr Size size() const noexcept { return {width, height}; }

    /// Shrinks the rect by `amount` on every side (grows it when negative), never below zero size.
    ///
    /// @param amount Points to move each edge inward.
    /// @return The inset rect. When it would be narrower or shorter than zero, its width or height is
    ///   0 and its origin still moves by `amount`.
    [[nodiscard]] constexpr Rect inset(float amount) const noexcept {
        return {x + amount, y + amount, std::max(0.f, width - 2 * amount),
                std::max(0.f, height - 2 * amount)};
    }

    friend constexpr bool operator==(const Rect&, const Rect&) = default;
};

/// A radius in points for each corner of a rounded rectangle; 0 is a square corner. When drawn, each
/// radius is clamped to half the rectangle's shorter side.
struct CornerRadii {
    float top_left = 0;     ///< Top-left corner radius.
    float top_right = 0;    ///< Top-right corner radius.
    float bottom_right = 0; ///< Bottom-right corner radius.
    float bottom_left = 0;  ///< Bottom-left corner radius.

    /// The same radius on all four corners.
    ///
    /// @param radius The radius in points.
    /// @return Radii with every corner set to `radius`.
    static constexpr CornerRadii all(float radius) noexcept {
        return {radius, radius, radius, radius};
    }
};

} // namespace elg

#endif // ELG_GEOMETRY_H
