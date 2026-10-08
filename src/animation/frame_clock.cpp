// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include "view/animated.h"

namespace elg::detail {

namespace {

double g_frame_time = 0;
const Animation* g_layout_animation = nullptr;

} // namespace

double frame_time() noexcept {
    return g_frame_time;
}

void set_frame_time(double seconds) noexcept {
    g_frame_time = seconds;
}

const Animation* layout_animation() noexcept {
    return g_layout_animation;
}

LayoutAnimationScope::LayoutAnimationScope(const Animation* animation) noexcept : previous_(g_layout_animation) {
    g_layout_animation = animation;
}

LayoutAnimationScope::~LayoutAnimationScope() {
    g_layout_animation = previous_;
}

} // namespace elg::detail
