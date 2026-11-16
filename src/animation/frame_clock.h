// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_ANIMATION_FRAME_CLOCK_H
#define ELG_ANIMATION_FRAME_CLOCK_H

// The frame clock (design.md §9.5): the instant every animation in a frame samples, and the
// animation that layout changes in the frame take.

#include <elg/animation.h>

namespace elg::detail {

// The current frame's time in seconds (steady clock), set by the app once per frame so every
// animation in a frame samples the same instant.
double frame_time() noexcept;
void set_frame_time(double seconds) noexcept;

// The animation for layout changes in the frame being built (an animated transaction's), or
// nullptr. Set by the app around layout.
const Animation* layout_animation() noexcept;

class LayoutAnimationScope {
public:
    explicit LayoutAnimationScope(const Animation* animation) noexcept;
    ~LayoutAnimationScope();
    LayoutAnimationScope(const LayoutAnimationScope&) = delete;
    LayoutAnimationScope& operator=(const LayoutAnimationScope&) = delete;
    LayoutAnimationScope(LayoutAnimationScope&&) = delete;
    LayoutAnimationScope& operator=(LayoutAnimationScope&&) = delete;

private:
    const Animation* previous_;
};

} // namespace elg::detail

#endif // ELG_ANIMATION_FRAME_CLOCK_H
