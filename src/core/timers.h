// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_CORE_TIMERS_H
#define ELG_CORE_TIMERS_H

// The App's side of timers (elg/timer.h) and view lifecycle actions (View::on_appear and
// on_disappear). UI thread.

#include <functional>
#include <optional>

namespace elg::detail {

// When the next timer is due (steady clock seconds), if any is running.
std::optional<double> next_timer_due();
// Runs the timers due at `now`. Returns true if any ran.
bool run_due_timers(double now);
// Stops every timer (the App is done).
void cancel_all_timers() noexcept;

// on_appear and on_disappear actions run after the update that mounted or removed their view,
// never in the middle of building or tearing down views.
void queue_lifecycle_action(std::function<void()> action);
bool has_lifecycle_actions() noexcept;
// Runs what's queued, including what those actions queue. Returns true if any ran.
bool run_lifecycle_actions();
void clear_lifecycle_actions() noexcept;

} // namespace elg::detail

#endif // ELG_CORE_TIMERS_H
