// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include <elg/error.h>
#include <elg/prop.h>
#include <elg/reactive.h>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using namespace elg;

namespace {

// Effects need an owner; views get one from the framework, tests make their own.
struct Owner {
    detail::Scope scope;
    detail::Scope::Use use{scope};
};

} // namespace

TEST_CASE("State reads and writes through shared handles") {
    State<int> count(1);
    const State<int> copy = count; // same value
    copy.set(5);
    CHECK(count() == 5);
    count += 2;
    CHECK(copy.peek() == 7);
    count = 0;
    CHECK(count.get() == 0);
}

TEST_CASE("an effect runs once immediately, then after a flush when its state changed") {
    Owner owner;
    State<int> count(0);
    std::vector<int> seen;
    effect([&] { seen.push_back(count()); });
    REQUIRE(seen == std::vector<int>{0});

    count = 1;
    CHECK(seen.size() == 1); // writes don't run effects synchronously
    detail::flush_effects();
    CHECK(seen == std::vector<int>{0, 1});

    count = 1; // unchanged value: no notification
    detail::flush_effects();
    CHECK(seen.size() == 2);
}

TEST_CASE("computed values are lazy and memoized") {
    State<int> count(2);
    int runs = 0;
    const auto doubled = computed([&] {
        ++runs;
        return count() * 2;
    });
    CHECK(runs == 0); // not computed until read
    CHECK(doubled() == 4);
    CHECK(doubled() == 4);
    CHECK(runs == 1);
    count = 3;
    CHECK(runs == 1); // still lazy
    CHECK(doubled() == 6);
    CHECK(runs == 2);
}

TEST_CASE("updates are glitch-free across a diamond") {
    // a -> b, a -> c, (b, c) -> effect: the effect must never see b and c out of sync, and must
    // run exactly once per change of a.
    Owner owner;
    State<int> a(1);
    const auto b = computed([=] { return a() + 1; });
    const auto c = computed([=] { return a() * 10; });
    std::vector<std::pair<int, int>> seen;
    effect([&] { seen.emplace_back(b(), c()); });

    a = 2;
    detail::flush_effects();
    a = 3;
    detail::flush_effects();
    CHECK(seen == std::vector<std::pair<int, int>>{{2, 10}, {3, 20}, {4, 30}});
}

TEST_CASE("a computed that doesn't change stops propagation") {
    Owner owner;
    State<int> n(1);
    const auto positive = computed([=] { return n() > 0; });
    int runs = 0;
    effect([&] {
        (void)positive();
        ++runs;
    });
    n = 2; // still positive
    detail::flush_effects();
    CHECK(runs == 1);
    n = -1;
    detail::flush_effects();
    CHECK(runs == 2);
}

TEST_CASE("batch groups writes into one effect run") {
    Owner owner;
    State<int> x(0);
    State<int> y(0);
    int runs = 0;
    effect([&] {
        (void)x();
        (void)y();
        ++runs;
    });
    batch([&] {
        x = 1;
        y = 1;
    });
    CHECK(runs == 2); // initial + one batched run
}

TEST_CASE("untrack reads without subscribing") {
    Owner owner;
    State<int> tracked(0);
    State<int> ignored(0);
    int runs = 0;
    effect([&] {
        (void)tracked();
        (void)untrack([&] { return ignored(); });
        ++runs;
    });
    ignored = 1;
    detail::flush_effects();
    CHECK(runs == 1);
    tracked = 1;
    detail::flush_effects();
    CHECK(runs == 2);
}

TEST_CASE("effects created inside untrack still track their own reads") {
    Owner owner;
    State<int> outer_source(0);
    State<int> inner_source(0);
    int outer_runs = 0;
    int inner_runs = 0;
    effect([&] {
        (void)outer_source();
        ++outer_runs;
        // Like mounting a subtree: its reads must not subscribe the outer effect...
        untrack([&] {
            effect([&] {
                (void)inner_source();
                ++inner_runs;
            });
        });
    });
    inner_source = 1;
    detail::flush_effects();
    CHECK(outer_runs == 1); // ...but the inner effect still tracks inner_source.
    CHECK(inner_runs == 2);
}

TEST_CASE("disposing a scope ends its effects") {
    State<int> count(0);
    int runs = 0;
    {
        Owner owner;
        effect([&] {
            (void)count();
            ++runs;
        });
    }
    count = 1;
    detail::flush_effects();
    CHECK(runs == 1);
}

TEST_CASE("a Binding reads and writes its State") {
    State<std::string> name("Ada");
    const Binding<std::string> binding = name.binding();
    CHECK(binding() == "Ada");
    binding = "Grace";
    CHECK(name() == "Grace");

    State<int> count(1);
    const Binding<int> counter(count);
    counter += 4;
    CHECK(count() == 5);
}

TEST_CASE("Prop holds a static value or a reactive one") {
    const Prop<float> fixed = 0.5;
    REQUIRE(fixed.static_value());
    CHECK(*fixed.static_value() == 0.5f);

    State<float> level(0.25f);
    const Prop<float> from_state = level;
    CHECK(from_state.is_reactive());
    CHECK(from_state.get() == 0.25f);

    const Prop<std::string> text = [=] { return std::to_string(static_cast<int>(level() * 100)); };
    CHECK(text.get() == "25");
}

TEST_CASE("effects need an owner") {
    CHECK_THROWS_AS(effect([] {}), UsageError);
}

TEST_CASE("effects that keep re-triggering each other are reported") {
    Owner owner;
    State<int> a(0);
    State<int> b(0);
    effect([=] { b = a() + 1; });
    effect([=] { a = b() + 1; });
    CHECK_THROWS_AS(detail::flush_effects(), UsageError);
}

TEST_CASE("constant() makes a binding that ignores writes") {
    const Binding<bool> on = constant(true);
    on = false;
    CHECK(on());
    const Binding<std::string> name = constant<std::string>("Ada");
    CHECK(name() == "Ada");
}

TEST_CASE("a Signal reads a State, a Binding, or a closure, and works as a Prop") {
    Owner owner;
    State<bool> hovered(false);
    const Signal<bool> from_state = hovered;
    const Signal<bool> from_binding = hovered.binding();
    const Signal<bool> negated = [=] { return !hovered(); };
    CHECK_FALSE(from_state());
    CHECK_FALSE(from_binding());
    CHECK(negated());

    const Prop<bool> prop = from_state;
    CHECK(prop.is_reactive());
    std::vector<bool> seen;
    effect([&] { seen.push_back(prop.get()); }); // reading through the Signal subscribes
    hovered = true;
    detail::flush_effects();
    CHECK(seen == std::vector<bool>{false, true});
}
