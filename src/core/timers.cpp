// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include "core/timers.h"

#include <elg/reactive.h>
#include <elg/timer.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <memory>
#include <utility>
#include <vector>

namespace elg {

namespace detail {

struct TimerState {
    double due;      // steady clock seconds
    double interval; // every: seconds between runs; after: 0
    std::function<void()> action;
    bool done = false;
};

namespace {

std::vector<std::shared_ptr<TimerState>> g_timers;
std::vector<std::function<void()>> g_lifecycle;

double seconds_now() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

std::shared_ptr<TimerState> start(double seconds, double interval, std::function<void()> action) {
    check_ui_thread();
    auto state = std::make_shared<TimerState>(TimerState{
        seconds_now() + (std::isfinite(seconds) ? std::max(seconds, 0.0) : 0.0), interval, std::move(action)});
    if (!state->action) {
        state->done = true;
        return state;
    }
    // Started while building a view or in its on_appear: it ends with the view.
    if (Scope* scope = current_scope()) {
        scope->on_end([weak = std::weak_ptr(state)] {
            if (auto timer = weak.lock()) {
                timer->done = true;
            }
        });
    }
    g_timers.push_back(state);
    return state;
}

} // namespace

std::optional<double> next_timer_due() {
    std::optional<double> due;
    for (const auto& timer : g_timers) {
        if (!timer->done && (!due || timer->due < *due)) {
            due = timer->due;
        }
    }
    return due;
}

bool run_due_timers(double now) {
    std::erase_if(g_timers, [](const auto& timer) { return timer->done; });
    // Earliest first. A copy: actions may start and cancel timers.
    std::vector<std::shared_ptr<TimerState>> due;
    for (const auto& timer : g_timers) {
        if (timer->due <= now) {
            due.push_back(timer);
        }
    }
    std::ranges::stable_sort(due, {}, &TimerState::due);
    for (const auto& timer : due) {
        if (timer->done) {
            continue; // cancelled by an earlier one
        }
        if (timer->interval > 0) {
            // The next run, skipping any the app was too busy for.
            timer->due += timer->interval * std::max(1.0, std::floor((now - timer->due) / timer->interval) + 1);
        } else {
            timer->done = true;
        }
        // A copy: the action may cancel its own timer.
        const std::function<void()> action = timer->action;
        action();
    }
    return !due.empty();
}

void cancel_all_timers() noexcept {
    for (const auto& timer : g_timers) {
        timer->done = true;
    }
    g_timers.clear();
}

void queue_lifecycle_action(std::function<void()> action) {
    g_lifecycle.push_back(std::move(action));
}

bool has_lifecycle_actions() noexcept {
    return !g_lifecycle.empty();
}

bool run_lifecycle_actions() {
    bool ran = false;
    while (!g_lifecycle.empty()) {
        std::vector<std::function<void()>> actions;
        actions.swap(g_lifecycle);
        for (auto& action : actions) {
            action();
        }
        ran = true;
    }
    return ran;
}

void clear_lifecycle_actions() noexcept {
    g_lifecycle.clear();
}

} // namespace detail

void Timer::cancel() const noexcept {
    if (state_) {
        state_->done = true;
    }
}

bool Timer::active() const noexcept {
    return state_ && !state_->done;
}

Timer after(double seconds, std::function<void()> action) {
    return Timer(detail::start(seconds, 0, std::move(action)));
}

Timer every(double seconds, std::function<void()> action) {
    const double interval = std::isfinite(seconds) ? std::max(seconds, 0.001) : 0.001;
    return Timer(detail::start(interval, interval, std::move(action)));
}

} // namespace elg
