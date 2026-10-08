// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include "palette.h"

#include <elg/elegence.h>

namespace elg {

View View::card(float corner_radius) const {
    // The shadow is on the background shape alone, so it takes the analytic fast path.
    return padding(20)
        .background(RoundedRectangle(corner_radius)
                        .fill(ThemeColor::surface)
                        .shadow({.color = Color::black.opacity(0.35f), .radius = 12, .y = 4}))
        .overlay(RoundedRectangle(corner_radius).stroke_border(ThemeColor::outline, 1));
}

} // namespace elg
