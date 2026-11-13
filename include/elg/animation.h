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

/// A timing curve over time: how an animated change moves from where it was to its target. A
/// small value, cheap to copy. Use it with with_animation(), View::animation() and transitions.
class ELG_API Animation {
public:
    /// Constant speed, from start to end.
    ///
    /// @param duration Seconds; negative counts as 0 (no animation).
    /// @return The animation.
    static Animation linear(double duration = 0.25);
    /// Starts slowly and speeds up: CSS's `ease-in`, cubic-bezier(0.42, 0, 1, 1).
    ///
    /// @param duration Seconds; negative counts as 0 (no animation).
    /// @return The animation.
    static Animation ease_in(double duration = 0.25);
    /// Starts quickly and slows down at the end: CSS's `ease-out`, cubic-bezier(0, 0, 0.58, 1).
    ///
    /// @param duration Seconds; negative counts as 0 (no animation).
    /// @return The animation.
    static Animation ease_out(double duration = 0.25);
    /// Slow at both ends: CSS's `ease-in-out`, cubic-bezier(0.42, 0, 0.58, 1).
    ///
    /// @param duration Seconds; negative counts as 0 (no animation).
    /// @return The animation.
    static Animation ease_in_out(double duration = 0.35);
    /// A spring settling at the target. Same meaning as SwiftUI's
    /// .spring(response:dampingFraction:). It runs until it has settled, which can take longer
    /// than `response`.
    ///
    /// @param response Roughly the time to get there, in seconds (at least 0.01).
    /// @param damping_fraction 1 settles without overshoot; lower values bounce (at least 0.01).
    /// @return The animation.
    static Animation spring(double response = 0.5, double damping_fraction = 0.825);
    /// A gentle spring without overshoot.
    ///
    /// @param duration Roughly the time to get there, in seconds: the spring's response.
    /// @return The animation.
    static Animation smooth(double duration = 0.4) { return spring(duration, 1.0); }
    /// Your own curve: `curve` gets the fraction of `duration` elapsed (0 to 1) and returns the
    /// progress (0 at the start, 1 at the target). It may go past 0 or 1 to overshoot; the
    /// animation lands on the target once `duration` is up regardless.
    ///
    /// @code
    /// Animation::custom([](double t) { return t * t * t; }, 0.3)
    /// @endcode
    ///
    /// @param curve Maps elapsed fraction to progress. Empty, or where it returns a value that
    ///   isn't finite: linear.
    /// @param duration Seconds; negative counts as 0 (no animation).
    /// @return The animation.
    static Animation custom(std::function<double(double)> curve, double duration = 0.25);
    /// A cubic Bézier through (0, 0), (x1, y1), (x2, y2) and (1, 1), like CSS's cubic-bezier():
    ///
    /// @code
    /// Animation::timing_curve(0.55, 0.1, 0.45, 0.9, 1.4)
    /// @endcode
    ///
    /// @param x1 The first control point's time, 0 to 1 (clamped).
    /// @param y1 The first control point's progress; outside 0 to 1 overshoots.
    /// @param x2 The second control point's time, 0 to 1 (clamped).
    /// @param y2 The second control point's progress; outside 0 to 1 overshoots.
    /// @param duration Seconds; negative counts as 0 (no animation).
    /// @return The animation.
    static Animation timing_curve(double x1, double y1, double x2, double y2, double duration = 0.25);

    /// Waits before starting. A negative delay starts partway in, like CSS: delay(-0.1) begins
    /// where it would be 0.1 s in (staggering things that repeat).
    ///
    /// @param seconds Added to any delay it already has; not finite: ignored.
    /// @return This animation, starting `seconds` later.
    [[nodiscard]] Animation delay(double seconds) const;

    /// Plays `count` times in all. When done, it lands on the target.
    ///
    /// @param count How many times; less than 1 counts as 1.
    /// @param autoreverses Every other time runs backward, from the target to where it started.
    /// @return This animation, repeating.
    [[nodiscard]] Animation repeat_count(int count, bool autoreverses = true) const;
    /// Plays again and again until the value changes or the view goes away. A spinner turning
    /// forever: rotation 0 to 360, linear, without reversing.
    ///
    /// @param autoreverses Every other time runs backward, from the target to where it started.
    /// @return This animation, repeating forever.
    [[nodiscard]] Animation repeat_forever(bool autoreverses = true) const;

    /// Progress at `seconds` since the start: 0 at the start, 1 at the end (springs overshoot).
    /// Repeating ones go round again each time (back down while autoreversing).
    ///
    /// @param seconds Time since the animation started, delay included.
    /// @return The progress: 0 during the delay, 1 once it's done.
    [[nodiscard]] double progress(double seconds) const noexcept;
    /// Whether the animation is over at `seconds` since the start. Never for repeat_forever.
    ///
    /// @param seconds Time since the animation started, delay included.
    /// @return true once the delay and every play are over.
    [[nodiscard]] bool finished(double seconds) const noexcept;

private:
    enum class Curve { linear, ease_in, ease_out, ease_in_out, spring, custom };
    Animation(Curve curve, double duration, double damping) noexcept
        : curve_(curve), duration_(duration), damping_(damping) {}

    // One play: progress, its length, and whether it's done (`t`: seconds since it started).
    [[nodiscard]] double play_progress(double t) const noexcept;
    [[nodiscard]] double play_length() const noexcept;

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

/// Runs `changes` in an animated transaction: every property change that the writes inside cause
/// animates with `animation`, including layout and If/ForEach insertions and removals.
///
/// @code
/// with_animation(Animation::spring(), [&] { expanded = !expanded(); });
/// @endcode
///
/// Calls nest: inside an inner call its animation applies, and once it returns the outer call's
/// animation applies again to the outer call's later writes.
///
/// @param animation How the changes animate.
/// @param changes Runs once, now; the state it writes animates. Exceptions propagate.
/// @throws UsageError Off the UI thread of a running App.
template <std::invocable F>
void with_animation(const Animation& animation, F&& changes) {
    std::shared_ptr<const Animation> previous = detail::begin_transaction(std::make_shared<const Animation>(animation));
    try {
        std::invoke(std::forward<F>(changes));
    } catch (...) {
        detail::end_transaction(std::move(previous));
        throw;
    }
    detail::end_transaction(std::move(previous));
}

/// How a view enters and leaves when an If/ForEach adds or removes it with an animation.
/// The default is a fade. Set with View::transition():
///
/// @code
/// If(shown, Panel().transition(Transition::move(Edges::bottom)))
/// @endcode
class ELG_API Transition {
public:
    /// Fades in and out.
    ///
    /// @return The transition.
    static Transition opacity();
    /// Appears and disappears at once.
    ///
    /// @return The transition.
    static Transition identity();
    /// Grows from (and shrinks to) `from` times its size, around its center or the point of its
    /// frame `anchor` names: Transition::scale(0.9f, Alignment::top) grows down from its top edge.
    ///
    /// @param from The scale it starts from and ends at: 0 is nothing, 1 its full size.
    /// @param anchor The point of its frame it scales around.
    /// @return The transition.
    static Transition scale(float from = 0.f, Alignment anchor = Alignment::center);
    /// Moves in from (and out to) an offset from its place.
    ///
    /// @param x The horizontal offset, in points.
    /// @param y The vertical offset, in points.
    /// @return The transition.
    static Transition offset(float x, float y);
    /// Slides in from (and out to) beyond an edge of its frame: Edges::top, leading, ...
    ///
    /// @param edge The edge; several (Edges::top | Edges::leading) move diagonally.
    /// @return The transition.
    static Transition move(Edges edge);
    /// Moves in from the leading edge and out to the trailing edge.
    ///
    /// @return The transition.
    static Transition slide();
    /// Different transitions for insertion and removal.
    ///
    /// @param insertion How the view enters.
    /// @param removal How it leaves.
    /// @return The transition.
    static Transition asymmetric(const Transition& insertion, const Transition& removal);

    /// Both at once: Transition::opacity().combined(Transition::scale(0.8f))
    ///
    /// @param other The transition to combine with this one.
    /// @return A transition that does both: opacities and scales multiply, offsets add.
    [[nodiscard]] Transition combined(const Transition& other) const;

    /// How the view looks at `progress` (0: fully out, 1: in place).
    struct Pose {
        float opacity = 1; ///< multiplies the view's opacity
        float scale = 1;   ///< multiplies its size, around the anchor
        float x = 0; ///< offset in points
        float y = 0; ///< offset in points
        /// What it scales around, as fractions of its frame (0.5, 0.5: the middle).
        float anchor_x = 0.5f;
        float anchor_y = 0.5f; ///< as `anchor_x`, vertically
    };
    /// How the view looks partway through the transition.
    ///
    /// @param progress 0: fully out, 1: in place.
    /// @param removal true for a view leaving (the removal transition), false for one entering.
    /// @param size The view's size, which edge moves are measured by.
    /// @return The view's opacity, scale and offset there.
    [[nodiscard]] Pose pose(float progress, bool removal, Size size) const noexcept;

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
