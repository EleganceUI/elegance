// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Float to integer conversions that can't be undefined. Converting a float that's NaN, infinite
// or out of the integer's range is undefined behavior; geometry from untrusted input (path data,
// sizes) can be any of these.

#ifndef ELG_CORE_NUMERIC_H
#define ELG_CORE_NUMERIC_H

#include <concepts>
#include <limits>

namespace elg::detail {

// `value` as an I in [lo, hi], truncated toward zero: NaN becomes 0, then the value is clamped
// before the conversion. Compared as a double: a value at or past a bound that a double can't hold
// exactly (as INT64_MAX) still converts to that bound, and every value strictly between the two
// truncates to an I within them.
template <std::integral I, std::floating_point F>
I clamped_cast(F value, I lo, I hi) noexcept {
    auto v = static_cast<double>(value);
    if (v != v) { // NaN
        v = 0;
    }
    if (v <= static_cast<double>(lo)) {
        return lo;
    }
    if (v >= static_cast<double>(hi)) {
        return hi;
    }
    return static_cast<I>(v);
}

// `value` as an I, clamped to I's whole range (NaN becomes 0).
template <std::integral I, std::floating_point F>
I clamped_cast(F value) noexcept {
    return clamped_cast<I>(value, std::numeric_limits<I>::min(), std::numeric_limits<I>::max());
}

} // namespace elg::detail

#endif // ELG_CORE_NUMERIC_H
