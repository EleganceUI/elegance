// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Float to integer conversions that can't be undefined. Converting a float that's NaN, infinite
// or out of the integer's range is undefined behavior; geometry from untrusted input (path data,
// sizes) can be any of these.

#ifndef ELG_CORE_NUMERIC_H
#define ELG_CORE_NUMERIC_H

#include <algorithm>
#include <concepts>

namespace elg::detail {

// `value` as an I in [lo, hi]: NaN becomes 0, then the value is clamped before the conversion.
// Clamped as a double, where every value of I is exact (a float can't hold UINT32_MAX).
template <std::integral I, std::floating_point F>
    requires(sizeof(I) <= 4)
I clamped_cast(F value, I lo, I hi) noexcept {
    auto v = static_cast<double>(value);
    if (v != v) { // NaN
        v = 0;
    }
    return static_cast<I>(std::clamp(v, static_cast<double>(lo), static_cast<double>(hi)));
}

} // namespace elg::detail

#endif // ELG_CORE_NUMERIC_H
