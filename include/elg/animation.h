// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_ANIMATION_H
#define ELG_ANIMATION_H

// Animation (design.md §9.5).
//
//   with_animation(Animation::spring(), [&] { expanded = !expanded(); });  // explicit
//   content.animation(Animation::ease_out(0.2), selected)                  // implicit
//   If(shown, Panel().transition(Transition::move(Edges::bottom)))          // insertion/removal
//   spinner.animation(Animation::linear(1).repeat_forever(false), spinning) // repeating
//
// Animated: colors, fills, strokes, text color, opacity, and layout (frames ease to their new
// position and size). If/ForEach children animate in and out with their transition.

#include <elg/export.h>
#include <elg/reactive.h>
#include <elg/view.h>

#include <functional>
#include <memory>

namespace elg {

// A timing curve over time.
class ELG_API Animation {
public:
    static Animation linear(double duration = 0.25);
    static Animation ease_in(double duration = 0.25);
    static Animation ease_out(double duration = 0.25);
    static Animation ease_in_out(double duration = 0.35);
    // A spring settling at the target. `response`: roughly the time to get there, in seconds.
    // `damping_fraction`: 1 settles without overshoot; lower values bounce. Same meaning as
    // SwiftUI's .spring(response:dampingFraction:).
    static Animation spring(double response = 0.5, double damping_fraction = 0.825);
    // A gentle spring without overshoot.
    static Animation smooth(double duration = 0.4) { return spring(duration, 1.0); }
    // Your own curve: `curve` gets the fraction of `duration` elapsed (0 to 1) and returns the
    // progress (0 at the start, 1 at the target). It may go past 0 or 1 to overshoot; the
    // animation lands on the target once `duration` is up regardless.
    //   Animation::custom([](double t) { return t * t * t; }, 0.3)
    static Animation custom(std::function<double(double)> curve, double duration = 0.25);
    // A cubic Bézier through (0, 0), (x1, y1), (x2, y2) and (1, 1), like CSS's cubic-bezier():
    //   Animation::timing_curve(0.55, 0.1, 0.45, 0.9, 1.4)
    static Animation timing_curve(double x1, double y1, double x2, double y2, double duration = 0.25);

    // Waits before starting. A negative delay starts partway in, like CSS: delay(-0.1) begins
    // where it would be 0.1 s in (staggering things that repeat).
    Animation delay(double seconds) const;

    // Plays `count` times in all. `autoreverses`: every other time runs backward, from the target
    // to where it started. When done, it lands on the target.
    Animation repeat_count(int count, bool autoreverses = true) const;
    // Plays again and again until the value changes or the view goes away. A spinner turning
    // forever: rotation 0 to 360, linear, without reversing.
    Animation repeat_forever(bool autoreverses = true) const;

    // Progress at `seconds` since the start: 0 at the start, 1 at the end (springs overshoot).
    // Repeating ones go round again each time (back down while autoreversing).
    double progress(double seconds) const noexcept;
    // Never for repeat_forever.
    bool finished(double seconds) const noexcept;

private:
    enum class Curve { linear, ease_in, ease_out, ease_in_out, spring, custom };
    Animation(Curve curve, double duration, double damping) noexcept
        : curve_(curve), duration_(duration), damping_(damping) {}

    // One play: progress, its length, and whether it's done (`t`: seconds since it started).
    double play_progress(double t) const noexcept;
    double play_length() const noexcept;

    Curve curve_;
    double duration_; // seconds; a spring's response
    double damping_;  // springs only
    double delay_ = 0;
    int plays_ = 1;   // -1: forever
    bool autoreverses_ = false;
    ELG_PRIVATE_MEMBERS_BEGIN
    std::shared_ptr<const std::function<double(double)>> custom_; // Curve::custom only
    ELG_PRIVATE_MEMBERS_END
};

// Runs `changes` in an animated transaction: every property change that the writes inside cause
// animates with `animation`, including layout and If/ForEach insertions and removals.
template <std::invocable F>
void with_animation(const Animation& animation, F&& changes) {
    detail::begin_transaction(std::make_shared<const Animation>(animation));
    try {
        std::invoke(std::forward<F>(changes));
    } catch (...) {
        detail::end_transaction();
        throw;
    }
    detail::end_transaction();
}

// How a view enters and leaves when an If/ForEach adds or removes it with an animation.
// The default is a fade.
class ELG_API Transition {
public:
    static Transition opacity();
    static Transition identity(); // appears and disappears at once
    // Grows from (and shrinks to) `from` times its size, around its center or the point of its
    // frame `anchor` names: Transition::scale(0.9f, Alignment::top) grows down from its top edge.
    static Transition scale(float from = 0.f, Alignment anchor = Alignment::center);
    static Transition offset(float x, float y);
    // Slides in from (and out to) beyond an edge of its frame: Edges::top, leading, ...
    static Transition move(Edges edge);
    // Moves in from the leading edge and out to the trailing edge.
    static Transition slide();
    // Different transitions for insertion and removal.
    static Transition asymmetric(const Transition& insertion, const Transition& removal);

    // Both at once: Transition::opacity().combined(Transition::scale(0.8f))
    Transition combined(const Transition& other) const;

    // How the view looks at `progress` (0: fully out, 1: in place).
    struct Pose {
        float opacity = 1;
        float scale = 1;
        float x = 0; // offset in points
        float y = 0;
        // What it scales around, as fractions of its frame (0.5, 0.5: the middle).
        float anchor_x = 0.5f;
        float anchor_y = 0.5f;
    };
    Pose pose(float progress, bool removal, Size size) const noexcept;

private:
    struct Effect {
        float opacity = 1;
        float scale = 1;
        float x = 0;
        float y = 0;
        // Edges to move beyond, by the frame's size.
        float edge_x = 0; // -1: leading, 1: trailing
        float edge_y = 0; // -1: top, 1: bottom
        float anchor_x = 0.5f;
        float anchor_y = 0.5f;
    };
    static Effect merge(const Effect& a, const Effect& b) noexcept;

    Effect insertion_;
    Effect removal_;
};

} // namespace elg

#endif // ELG_ANIMATION_H
