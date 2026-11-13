// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_TIMER_H
#define ELG_TIMER_H

// Timers (design.md §9.5): actions on the UI thread after a delay or at an interval.
//
//   .on_appear([=] { every(1, [=] { seconds += 1; }); })  // ends with the view
//   const Timer hide = after(3, [=] { loading = false; });
//   hide.cancel();
//
// A timer started while views are built, or in on_appear, belongs to that view: it stops when
// the view goes away. Started anywhere else (a click), it runs until it has fired (after) or is
// cancelled (every). State written in the action takes effect like a click's.

#include <elg/export.h>

#include <functional>
#include <memory>

namespace elg {

namespace detail {
struct TimerState;
} // namespace detail

/// A started timer, from after() or every(). Copies refer to the same one; dropping them doesn't
/// stop it. A default-constructed Timer refers to none: it's never active.
class ELG_API Timer {
public:
    Timer() = default;

    /// Stops it; the action doesn't run again. Fine to call more than once.
    void cancel() const noexcept;
    /// Whether the timer is still going: not cancelled, its view is still there, and for
    /// `after`, not fired yet.
    ///
    /// @return true while it's going.
    [[nodiscard]] bool active() const noexcept;

private:
    friend ELG_API Timer after(double seconds, std::function<void()> action);
    friend ELG_API Timer every(double seconds, std::function<void()> action);
    explicit Timer(std::shared_ptr<detail::TimerState> state) noexcept : state_(std::move(state)) {}

    ELG_PRIVATE_MEMBERS_BEGIN
    std::shared_ptr<detail::TimerState> state_;
    ELG_PRIVATE_MEMBERS_END
};

/// Runs `action` once, `seconds` from now (0: as soon as the current event is handled). UI thread.
///
/// @code
/// const Timer hide = after(3, [=] { loading = false; });
/// @endcode
///
/// @param seconds The delay; negative (including negative infinity) counts as 0. Positive
///   infinity never runs `action`: the timer stays active until it's cancelled or its scope ends.
/// @param action Runs on the UI thread. Empty: the timer is never active.
/// @return The timer, to cancel() it.
/// @throws UsageError When `seconds` is NaN, or off the UI thread of a running App.
ELG_API Timer after(double seconds, std::function<void()> action);
/// Runs `action` every `seconds` (at least a millisecond), the first time `seconds` from now. If
/// the app falls behind, missed runs are skipped rather than run back to back. UI thread.
///
/// @code
/// .on_appear([=] { every(1, [=] { seconds += 1; }); })  // ends with the view
/// @endcode
///
/// @param seconds The interval; less than a millisecond (including negative infinity) counts as a
///   millisecond. Positive infinity never runs `action`: the timer stays active until it's
///   cancelled or its scope ends.
/// @param action Runs on the UI thread. Empty: the timer is never active.
/// @return The timer, to cancel() it.
/// @throws UsageError When `seconds` is NaN, or off the UI thread of a running App.
ELG_API Timer every(double seconds, std::function<void()> action);

} // namespace elg

#endif // ELG_TIMER_H
