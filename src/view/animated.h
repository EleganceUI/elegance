// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_VIEW_ANIMATED_H
#define ELG_VIEW_ANIMATED_H

// Animated element properties (design.md §9.5): values that ease from where they are to a new
// target when set during an animated transaction, evaluated at the frame's time.

#include <elg/animation.h>
#include <elg/color.h>
#include <elg/geometry.h>
#include <elg/gradient.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <utility>

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

private:
    const Animation* previous_;
};

// `.animation(animation, value)` on a subtree: changes inside animate when the value changed in
// the same update.
struct ImplicitAnimation {
    Animation animation;
    std::uint64_t fired_flush = 0;

    bool fired() const noexcept { return fired_flush != 0 && fired_flush == current_flush(); }
};

inline float interpolate(float a, float b, double t) {
    return a + (b - a) * static_cast<float>(t);
}

inline Color interpolate(const Color& a, const Color& b, double t) {
    return {interpolate(a.r, b.r, t), interpolate(a.g, b.g, t), interpolate(a.b, b.b, t), interpolate(a.a, b.a, t)};
}

inline Rect interpolate(const Rect& a, const Rect& b, double t) {
    return {interpolate(a.x, b.x, t), interpolate(a.y, b.y, t), interpolate(a.width, b.width, t),
            interpolate(a.height, b.height, t)};
}

// Solid colors blend; anything involving a gradient switches to the target.
inline ShapeStyle interpolate(const ShapeStyle& a, const ShapeStyle& b, double t) {
    const Color* from = std::get_if<Color>(&a.value());
    const Color* to = std::get_if<Color>(&b.value());
    if (from && to) {
        return interpolate(*from, *to, t);
    }
    return b;
}

template <class T>
class Animated {
public:
    explicit Animated(T initial) : from_(initial), to_(std::move(initial)) {}

    // Jumps to `value`, or with an animation, eases there from the current value.
    void set(T value, const Animation* animation) {
        if (!animation) {
            from_ = value;
            to_ = std::move(value);
            animation_.reset();
            return;
        }
        from_ = current();
        to_ = std::move(value);
        animation_ = *animation;
        start_ = frame_time();
    }

    T current() const {
        if (!animation_) {
            return to_;
        }
        const double t = frame_time() - start_;
        return animation_->finished(t) ? to_ : interpolate(from_, to_, animation_->progress(t));
    }

    bool animating() const { return animation_ && !animation_->finished(frame_time() - start_); }
    const T& target() const noexcept { return to_; }

private:
    T from_;
    T to_;
    std::optional<Animation> animation_;
    double start_ = 0;
};

} // namespace elg::detail

#endif // ELG_VIEW_ANIMATED_H
