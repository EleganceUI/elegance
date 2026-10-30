// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// on_appear, on_disappear, timers, and repeating animations.

#include "core/timers.h"
#include "render/display_list.h"
#include "view/animated.h"
#include "view/view_node.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <elg/elegance.h>

#include <cmath>
#include <string>
#include <variant>
#include <vector>

using namespace elg;
using Catch::Approx;

namespace {

// Starts and ends with no timers or queued lifecycle actions.
struct Clean {
    Clean() { reset(); }
    ~Clean() { reset(); }
    static void reset() {
        detail::cancel_all_timers();
        detail::clear_lifecycle_actions();
    }
};

constexpr double kFarFuture = 1e12; // "now" for running every timer that's due by then

// A window's worth of views: `shown` decides whether the inner view is there.
struct Window {
    Clean clean;
    detail::Scope scope;
    detail::Scope::Use use{scope};
    State<bool> shown{true};
    std::unique_ptr<detail::Element> root;

    explicit Window(View inner) {
        const State<bool> visible = shown;
        root = detail::lower(VStack(If([=] { return visible(); }, std::move(inner))), {});
    }
};

} // namespace

TEST_CASE("repeat_count plays again, reversing every other time", "[animation]") {
    const Animation twice = Animation::linear(1.0).repeat_count(2);
    CHECK(twice.progress(0.25) == Approx(0.25));
    CHECK(twice.progress(1.25) == Approx(0.75)); // on the way back
    CHECK_FALSE(twice.finished(1.9));
    CHECK(twice.finished(2.0));
    CHECK(twice.progress(2.0) == 1); // lands on the target

    const Animation forward = Animation::linear(1.0).repeat_count(3, false);
    CHECK(forward.progress(2.25) == Approx(0.25));
    CHECK(forward.finished(3.0));
}

TEST_CASE("repeat_forever never finishes", "[animation]") {
    const Animation spin = Animation::linear(1.4).repeat_forever(false);
    CHECK_FALSE(spin.finished(1e6));
    CHECK(spin.progress(1.4 * 10 + 0.7) == Approx(0.5));

    const Animation pulse = Animation::ease_in_out(1.0).repeat_forever();
    CHECK(pulse.progress(1.0 + 0.5) == Approx(0.5).margin(0.01));
    CHECK(pulse.progress(1.0 + 0.25) > 0.5); // coming back down from 1
}

TEST_CASE("a negative delay starts partway in", "[animation]") {
    const Animation staggered = Animation::linear(1.0).delay(-0.25);
    CHECK(staggered.progress(0) == Approx(0.25));
    CHECK(staggered.finished(0.75));
    CHECK(Animation::linear(1.0).repeat_forever(false).delay(-0.25).progress(0.8) == Approx(0.05));
}

TEST_CASE("timing_curve follows a CSS cubic-bezier", "[animation]") {
    const Animation linear = Animation::timing_curve(0.25, 0.25, 0.75, 0.75, 1.0);
    CHECK(linear.progress(0.3) == Approx(0.3).margin(0.001));
    const Animation orbit = Animation::timing_curve(0.55, 0.1, 0.45, 0.9, 1.0);
    CHECK(orbit.progress(0.5) == Approx(0.5).margin(0.001)); // symmetric
    CHECK(orbit.progress(0.2) < 0.2);                        // slow start
    CHECK(orbit.progress(1.0) == 1);
}

TEST_CASE("on_appear runs after the update that added the view", "[lifecycle]") {
    std::vector<std::string> log;
    Window window(Rectangle().on_appear([&] { log.push_back("appear"); }));
    CHECK(log.empty()); // not while building
    CHECK(detail::run_lifecycle_actions());
    CHECK(log == std::vector<std::string>{"appear"});
    CHECK_FALSE(detail::run_lifecycle_actions()); // once
}

TEST_CASE("on_disappear runs once the view is gone, only if it appeared", "[lifecycle]") {
    std::vector<std::string> log;
    Window window(Rectangle().on_appear([&] { log.push_back("appear"); }).on_disappear([&] { log.push_back("gone"); }));
    detail::run_lifecycle_actions();
    window.shown = false;
    detail::flush_effects();
    CHECK(log == std::vector<std::string>{"appear"}); // queued, not run while tearing down
    detail::run_lifecycle_actions();
    CHECK(log == std::vector<std::string>{"appear", "gone"});

    // Shown and removed again before it got to appear: neither runs.
    log.clear();
    window.shown = true;
    detail::flush_effects();
    window.shown = false;
    detail::flush_effects();
    detail::run_lifecycle_actions();
    CHECK(log.empty());
}

TEST_CASE("a timer started in on_appear stops with the view", "[lifecycle][timer]") {
    int ticks = 0;
    Timer timer;
    Window window(Rectangle().on_appear([&] { timer = every(0.5, [&] { ++ticks; }); }));
    detail::run_lifecycle_actions();
    REQUIRE(timer.active());
    detail::run_due_timers(kFarFuture);
    CHECK(ticks == 1);

    window.shown = false;
    detail::flush_effects();
    CHECK_FALSE(timer.active());
    detail::run_due_timers(kFarFuture * 2);
    CHECK(ticks == 1);
    CHECK_FALSE(detail::next_timer_due());
}

TEST_CASE("after fires once; every repeats, skipping runs it missed", "[timer]") {
    Clean clean;
    int once = 0;
    int repeated = 0;
    const Timer single = after(0, [&] { ++once; });
    const Timer repeating = every(1, [&] { ++repeated; });
    REQUIRE(detail::next_timer_due());

    detail::run_due_timers(kFarFuture);
    CHECK(once == 1);
    CHECK(repeated == 1); // not once per missed second
    CHECK_FALSE(single.active());
    CHECK(repeating.active());
    CHECK(*detail::next_timer_due() > kFarFuture);

    detail::run_due_timers(kFarFuture + 2);
    CHECK(once == 1);
    CHECK(repeated == 2);
}

TEST_CASE("cancelled timers don't run, even from an earlier timer's action", "[timer]") {
    Clean clean;
    int runs = 0;
    Timer later;
    const Timer first = after(0, [&] { later.cancel(); });
    later = after(0.001, [&] { ++runs; });
    detail::run_due_timers(kFarFuture);
    CHECK(runs == 0);
    CHECK_FALSE(later.active());

    // A timer may cancel itself.
    Timer self;
    self = every(1, [&] {
        ++runs;
        self.cancel();
    });
    detail::run_due_timers(kFarFuture);
    detail::run_due_timers(kFarFuture * 2);
    CHECK(runs == 1);
}

TEST_CASE("timers started outside views run until cancelled", "[timer]") {
    Clean clean;
    int runs = 0;
    Timer timer;
    {
        detail::Scope scope; // ends, but the timer wasn't started in it
        timer = every(1, [&] { ++runs; });
    }
    detail::run_due_timers(kFarFuture);
    CHECK(runs == 1);
    timer.cancel();
    CHECK_FALSE(detail::next_timer_due());
}

TEST_CASE("a view started spinning in on_appear keeps turning", "[lifecycle][animation]") {
    Clean clean;
    detail::Scope scope;
    detail::Scope::Use use{scope};
    const State<bool> spinning(false);
    auto root = detail::lower(Circle()
                                  .fill(Color::white)
                                  .frame(12, 12)
                                  .rotation_effect([=] { return spinning() ? 360.f : 0.f; })
                                  .animation(Animation::linear(1.0).repeat_forever(false), spinning)
                                  .on_appear([=] { spinning = true; }),
                              {});
    // The angle drawn at `seconds`, and whether the frame asks for another.
    const auto angle_at = [&](double seconds, bool* animating = nullptr) {
        detail::set_frame_time(seconds);
        root->size_that_fits({12, 12});
        root->place({}, {12, 12});
        detail::DisplayList list;
        root->paint(list);
        if (animating) {
            *animating = list.animating;
        }
        for (const detail::DrawCommand& command : list.commands) {
            if (const auto* layer = std::get_if<detail::PushLayerCommand>(&command); layer && layer->transform) {
                return std::atan2(layer->transform->b, layer->transform->a) * 180 / 3.14159265f;
            }
        }
        return 0.f;
    };
    detail::set_frame_time(100);
    detail::run_lifecycle_actions();
    detail::flush_effects();
    bool animating = false;
    CHECK(angle_at(100.25, &animating) == Approx(90).margin(0.5));
    CHECK(animating);
    CHECK(angle_at(100.4) == Approx(144).margin(0.5));
    CHECK(angle_at(103.75) == Approx(-90).margin(0.5)); // 270, four turns on
    detail::set_frame_time(0);
}
