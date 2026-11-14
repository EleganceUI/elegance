// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include <elg/error.h>
#include <elg/prop.h>
#include <elg/reactive.h>

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
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
    const State<int> count(1);
    // NOLINTNEXTLINE(performance-unnecessary-copy-initialization): the copy is what's tested
    const State<int> copy = count; // same value
    copy.set(5);
    CHECK(count() == 5);
    count += 2;
    CHECK(copy.peek() == 7);
    count = 0;
    CHECK(count.get() == 0);
}

TEST_CASE("an effect runs once immediately, then after a flush when its state changed") {
    const Owner owner;
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
    const Owner owner;
    const State<int> a(1);
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
    const Owner owner;
    const State<int> n(1);
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
    const Owner owner;
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
    const Owner owner;
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
    const Owner owner;
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
        const Owner owner;
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
    const State<std::string> name("Ada");
    const Binding<std::string> binding = name.binding();
    CHECK(binding() == "Ada");
    binding = "Grace";
    CHECK(name() == "Grace");

    const State<int> count(1);
    const Binding<int> counter(count);
    counter += 4;
    CHECK(count() == 5);
}

TEST_CASE("Prop holds a static value or a reactive one") {
    const Prop<float> fixed = 0.5;
    REQUIRE(fixed.static_value());
    CHECK(*fixed.static_value() == 0.5f);

    const State<float> level(0.25f);
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
    const Owner owner;
    const State<int> a(0);
    const State<int> b(0);
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
    const Owner owner;
    const State<bool> hovered(false);
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

TEST_CASE("a computed that throws rethrows until what it read changes", "[reactive]") {
    detail::Scope scope;
    const detail::Scope::Use use(scope);
    const State<int> input(-1);
    int runs = 0;
    const auto doubled = computed([=, &runs] {
        ++runs;
        if (input() < 0) {
            throw std::runtime_error("negative");
        }
        return input() * 2;
    });
    CHECK_THROWS_AS(doubled(), std::runtime_error);
    CHECK_THROWS_AS(doubled(), std::runtime_error); // kept, not run again
    CHECK(runs == 1);
    input = 3;
    CHECK(doubled() == 6);
    CHECK(runs == 2);
}

TEST_CASE("a batch inside an effect leaves what it schedules until the effect is done", "[reactive]") {
    const Owner owner;
    const State<int> trigger(0);
    const State<int> written(0);
    std::vector<std::string> log;
    effect([=, &log] {
        if (trigger() > 0) {
            batch([=] { written = trigger(); });
            log.emplace_back("writer done");
        }
    });
    effect([=, &log] { log.push_back("reader " + std::to_string(written())); });
    log.clear();
    trigger = 1;
    detail::flush_effects();
    CHECK(log == std::vector<std::string>{"writer done", "reader 1"});
}

TEST_CASE("a flush asked for inside a flush leaves the effects to the one running", "[reactive]") {
    const Owner owner;
    const State<int> first(0);
    const State<int> second(0);
    std::vector<std::string> log;
    effect([=, &log] {
        if (first() > 0) {
            second = first();
            detail::flush_effects();
            log.emplace_back("first done");
        }
    });
    effect([=, &log] { log.push_back("second " + std::to_string(second())); });
    log.clear();
    first = 1;
    detail::flush_effects();
    CHECK(log == std::vector<std::string>{"first done", "second 1"});
}

TEST_CASE("no flush runs while effects are deferred", "[reactive]") {
    const Owner owner;
    const State<int> count(0);
    std::vector<int> seen;
    effect([=, &seen] { seen.push_back(count()); });
    {
        const detail::DeferEffects deferred;
        batch([=] { count = 1; });
        detail::flush_effects();
        CHECK(seen == std::vector<int>{0});
    }
    CHECK(seen == std::vector<int>{0}); // waiting for the next flush
    CHECK(detail::has_scheduled_effects());
    detail::flush_effects();
    CHECK(seen == std::vector<int>{0, 1});
}

TEST_CASE("an effect that throws leaves the effects after it scheduled", "[reactive]") {
    const Owner owner;
    const State<int> count(0);
    std::vector<int> seen;
    effect([=] {
        if (count() == 1) {
            throw std::runtime_error("failed");
        }
    });
    effect([=, &seen] { seen.push_back(count()); });
    count = 1;
    CHECK_THROWS_AS(detail::flush_effects(), std::runtime_error);
    CHECK(detail::has_scheduled_effects());
    detail::flush_effects(); // a later flush runs what's left
    CHECK(seen == std::vector<int>{0, 1});
}

TEST_CASE("an effect disposed while it runs doesn't subscribe to what it reads afterwards", "[reactive]") {
    auto scope = std::make_unique<detail::Scope>();
    const State<bool> stop(false);
    const State<int> later(0);
    detail::EffectNode* self = nullptr;
    {
        const detail::Scope::Use use(*scope);
        auto node = std::make_shared<detail::EffectNode>([&self, stop, later] {
            if (stop()) {
                self->dispose();
                (void)later();
            }
        });
        self = node.get();
        detail::start_effect(std::move(node));
    }
    stop = true;
    detail::flush_effects();
    scope.reset(); // frees the effect
    later = 1;     // subscribed, the freed effect would be scheduled (ASan reports the read)
    CHECK_FALSE(detail::has_scheduled_effects());
}

TEST_CASE("an effect's later runs create effects in the scope that owns it", "[reactive]") {
    auto scope = std::make_unique<detail::Scope>();
    const State<bool> on(false);
    const State<int> count(0);
    int inner_runs = 0;
    {
        const detail::Scope::Use use(*scope);
        effect([=, &inner_runs] {
            if (on()) {
                effect([=, &inner_runs] {
                    (void)count();
                    ++inner_runs;
                });
            }
        });
    }
    on = true;
    REQUIRE_NOTHROW(detail::flush_effects()); // it has an owner: the outer effect's
    CHECK(inner_runs == 1);
    count = 1;
    detail::flush_effects();
    CHECK(inner_runs == 2);
    scope.reset(); // ends both
    count = 2;
    detail::flush_effects();
    CHECK(inner_runs == 2);
}
