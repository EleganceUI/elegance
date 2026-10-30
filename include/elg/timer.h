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

// A started timer. Copies refer to the same one; dropping them doesn't stop it.
class ELG_API Timer {
public:
    Timer() = default;

    // Stops it; the action doesn't run again. Fine to call more than once.
    void cancel() const noexcept;
    // Still going: not cancelled, its view is still there, and for `after`, not fired yet.
    bool active() const noexcept;

private:
    friend ELG_API Timer after(double seconds, std::function<void()> action);
    friend ELG_API Timer every(double seconds, std::function<void()> action);
    explicit Timer(std::shared_ptr<detail::TimerState> state) noexcept : state_(std::move(state)) {}

    ELG_PRIVATE_MEMBERS_BEGIN
    std::shared_ptr<detail::TimerState> state_;
    ELG_PRIVATE_MEMBERS_END
};

// Runs `action` once, `seconds` from now (0: as soon as the current event is handled). UI thread.
ELG_API Timer after(double seconds, std::function<void()> action);
// Runs `action` every `seconds` (at least a millisecond), the first time `seconds` from now. If
// the app falls behind, missed runs are skipped rather than run back to back. UI thread.
ELG_API Timer every(double seconds, std::function<void()> action);

} // namespace elg

#endif // ELG_TIMER_H
