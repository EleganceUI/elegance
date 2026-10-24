// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include <elg/animation.h>
#include <elg/reactive.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <vector>

using namespace elg;
using Catch::Approx;

namespace {

struct Owner {
    detail::Scope scope;
    detail::Scope::Use use{scope};
};

double peak(const Animation& animation, double until) {
    double highest = 0;
    for (double t = 0; t <= until; t += 0.001) {
        highest = std::max(highest, animation.progress(t));
    }
    return highest;
}

} // namespace

TEST_CASE("timing curves run from 0 to 1 over their duration") {
    const Animation linear = Animation::linear(1.0);
    CHECK(linear.progress(0) == 0);
    CHECK(linear.progress(0.5) == Approx(0.5));
    CHECK(linear.progress(1.0) == 1);
    CHECK_FALSE(linear.finished(0.99));
    CHECK(linear.finished(1.0));

    const Animation ease = Animation::ease_in_out(1.0);
    CHECK(ease.progress(0.5) == Approx(0.5).margin(0.01)); // symmetric
    CHECK(ease.progress(0.25) < 0.25);                     // slow start
    CHECK(Animation::ease_out(1.0).progress(0.25) > 0.25); // fast start
}

TEST_CASE("custom curves map elapsed time to progress") {
    const Animation cubic = Animation::custom([](double t) { return t * t * t; }, 1.0);
    CHECK(cubic.progress(0) == 0);
    CHECK(cubic.progress(0.5) == Approx(0.125));
    CHECK(cubic.progress(1.0) == 1);
    CHECK(cubic.finished(1.0));

    // Overshoot passes through; the end still lands on the target.
    const Animation back = Animation::custom([](double t) { return t < 1 ? t * 1.5 : 7.0; }, 1.0);
    CHECK(back.progress(0.8) == Approx(1.2));
    CHECK(back.progress(1.0) == 1);

    CHECK(Animation::custom(nullptr, 1.0).progress(0.5) == Approx(0.5)); // no curve: linear
    CHECK(Animation::custom([](double t) { return t; }, 1.0).delay(0.5).progress(1.0) == Approx(0.5));
}

TEST_CASE("delay holds the start") {
    const Animation delayed = Animation::linear(1.0).delay(0.5);
    CHECK(delayed.progress(0.4) == 0);
    CHECK(delayed.progress(1.0) == Approx(0.5));
    CHECK_FALSE(delayed.finished(1.4));
    CHECK(delayed.finished(1.5));
}

TEST_CASE("springs settle at the target; damping 1 never overshoots") {
    const Animation critical = Animation::spring(0.5, 1.0);
    CHECK(peak(critical, 3.0) <= 1.0 + 1e-6);
    CHECK(critical.finished(3.0));
    CHECK(critical.progress(3.0) == Approx(1.0).margin(0.001));

    const Animation bouncy = Animation::spring(0.5, 0.4);
    CHECK(peak(bouncy, 3.0) > 1.05); // overshoots...
    CHECK(bouncy.progress(5.0) == Approx(1.0).margin(0.001)); // ...then settles

    const Animation overdamped = Animation::spring(0.5, 2.0);
    CHECK(peak(overdamped, 5.0) <= 1.0 + 1e-6);
}

TEST_CASE("transitions describe the out pose and the in-place identity") {
    const Transition fade = Transition::opacity();
    CHECK(fade.pose(0, false, {100, 50}).opacity == 0);
    CHECK(fade.pose(1, false, {100, 50}).opacity == 1);

    const Transition from_bottom = Transition::move(Edges::bottom);
    const Transition::Pose out = from_bottom.pose(0, false, {100, 50});
    CHECK(out.y == 50); // one frame height below
    CHECK(out.x == 0);
    CHECK(from_bottom.pose(1, false, {100, 50}).y == 0);

    const Transition slide = Transition::slide();
    CHECK(slide.pose(0, false, {100, 50}).x == -100); // in from leading
    CHECK(slide.pose(0, true, {100, 50}).x == 100);   // out to trailing

    const Transition both = Transition::opacity().combined(Transition::scale(0.5f));
    CHECK(both.pose(0, false, {10, 10}).opacity == 0);
    CHECK(both.pose(0, false, {10, 10}).scale == Approx(0.5));
}

TEST_CASE("effects scheduled inside with_animation run with its animation") {
    Owner owner;
    State<int> animated(0);
    State<int> plain(0);
    std::vector<std::string> seen;
    effect([&] {
        (void)animated();
        seen.push_back(detail::current_animation() ? "animated" : "jump");
    });
    effect([&] {
        (void)plain();
        seen.push_back(detail::current_animation() ? "animated" : "jump");
    });
    seen.clear();

    with_animation(Animation::linear(0.3), [&] { animated = 1; });
    plain = 1;
    detail::flush_effects();

    CHECK(seen == std::vector<std::string>{"animated", "jump"});
    CHECK(detail::take_flush_animation() != nullptr);
    CHECK(detail::take_flush_animation() == nullptr); // taken
    CHECK(detail::current_animation() == nullptr);    // only while an effect runs
}

TEST_CASE("priority effects run first in a flush") {
    Owner owner;
    State<int> source(0);
    std::vector<std::string> order;
    effect([&] {
        (void)source();
        order.push_back("normal");
    });
    detail::start_effect(std::make_shared<detail::EffectNode>([&] {
                             (void)source();
                             order.push_back("priority");
                         }),
                         true);
    order.clear();
    source = 1;
    detail::flush_effects();
    CHECK(order == std::vector<std::string>{"priority", "normal"});
}
