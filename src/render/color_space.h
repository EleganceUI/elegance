// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_RENDER_COLOR_SPACE_H
#define ELG_RENDER_COLOR_SPACE_H

#include <elg/color.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace elg::detail {

inline float srgb_to_linear(float c) {
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

// What shaders consume: linear light, premultiplied alpha.
inline std::array<float, 4> to_linear_premultiplied(Color color) {
    const float a = std::clamp(color.a, 0.f, 1.f);
    return {srgb_to_linear(color.r) * a, srgb_to_linear(color.g) * a, srgb_to_linear(color.b) * a, a};
}

} // namespace elg::detail

#endif // ELG_RENDER_COLOR_SPACE_H
