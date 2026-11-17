// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Read-only arguments are Props: a plain value, or state the view follows.

#include "view/view_node.h"

#include <elg/shapes.h>
#include <elg/views.h>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using namespace elg;

namespace {
bool same(Size a, Size b) {
    return a.width == b.width && a.height == b.height;
}
} // namespace

TEST_CASE("padding follows the state it's bound to", "[prop]") {
    detail::Scope scope;
    const detail::Scope::Use use(scope);
    const State<float> inset(10.f);
    auto root = detail::lower(Rectangle().frame(20, 20).padding(inset), {});
    CHECK(same(root->size_that_fits({}), {40, 40}));

    inset = 4.f;
    detail::flush_effects();
    CHECK(same(root->size_that_fits({}), {28, 28}));

    const State<float> top(0.f);
    auto edges = detail::lower(Rectangle().frame(20, 20).padding(Edges::top, top), {});
    CHECK(same(edges->size_that_fits({}), {20, 20}));
    top = 6.f;
    detail::flush_effects();
    CHECK(same(edges->size_that_fits({}), {20, 26}));
}

TEST_CASE("plain padding values still take braces", "[prop]") {
    detail::Scope scope;
    const detail::Scope::Use use(scope);
    auto root = detail::lower(Rectangle().frame(20, 20).padding({.top = 1, .leading = 2, .bottom = 3, .trailing = 4}), {});
    CHECK(same(root->size_that_fits({}), {26, 24}));
}

TEST_CASE("a slider's range follows state", "[prop]") {
    detail::Scope scope;
    const detail::Scope::Use use(scope);
    const State<float> value(5.f);
    const State<float> max(10.f);
    auto root = detail::lower(Slider(value.binding(), {.min = 0, .max = max}), {});
    detail::AccessibilityInfo info;
    root->accessibility(info);
    CHECK(info.maximum == 10.0);

    max = 50.f;
    detail::flush_effects();
    info = {};
    root->accessibility(info);
    CHECK(info.maximum == 50.0);
    CHECK(value() == 5.f); // a new range doesn't write the value
}

TEST_CASE("a lazy stack draws separators between its items", "[prop]") {
    detail::Scope scope;
    const detail::Scope::Use use(scope);
    const State<Color> line(Color::black);
    const View rows = LazyVStack({.spacing = 10, .separators = StackSeparators{.color = line, .inset = 4}},
                                 ForEach(std::vector<int>{1, 2, 3}, [](int) { return Rectangle().frame(20, 20); }));
    auto root = detail::lower(rows, {});
    root->place({}, {100.f, 200.f});
    detail::DisplayList list;
    root->paint(list);
    std::vector<Rect> lines;
    for (const auto& command : list.commands) {
        if (const auto* rect = std::get_if<detail::RectCommand>(&command); rect && rect->rect.height == 1) {
            lines.push_back(rect->rect);
        }
    }
    REQUIRE(lines.size() == 2); // between items, not after the last
    CHECK(lines[0].x == 4);
    CHECK(lines[0].width == 96);
    CHECK(lines[0].y == 24.5f); // in the middle of the spacing after the first item
}

TEST_CASE("Prop holds a static value or a reactive one", "[prop]") {
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

TEST_CASE("a Signal works as a Prop, which subscribes through it", "[prop]") {
    detail::Scope scope;
    const detail::Scope::Use use(scope);
    const State<bool> hovered(false);
    const Signal<bool> from_state = hovered;
    const Prop<bool> prop = from_state;
    CHECK(prop.is_reactive());
    std::vector<bool> seen;
    effect([&] { seen.push_back(prop.get()); }); // reading through the Signal subscribes
    hovered = true;
    detail::flush_effects();
    CHECK(seen == std::vector<bool>{false, true});
}
