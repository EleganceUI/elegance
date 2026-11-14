// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// A function that needs its callback throws UsageError when it's given an empty one, and takes a
// non-empty one (ELGSPEC-0010.R1). Those that can do without take an empty one.

#include <elg/animation.h>
#include <elg/app.h>
#include <elg/environment.h>
#include <elg/error.h>
#include <elg/gestures.h>
#include <elg/navigation.h>
#include <elg/prop.h>
#include <elg/reactive.h>
#include <elg/shapes.h>
#include <elg/views.h>

#include <catch2/catch_test_macros.hpp>

#include <functional>
#include <string>
#include <vector>

using namespace elg;

namespace {

// Effects need an owner; views get one from the framework, tests make their own.
struct Owner {
    detail::Scope scope;
    detail::Scope::Use use{scope};
};

struct Item {
    int id = 0;
};

View Row(int value) {
    return Text(std::to_string(value));
}

} // namespace

TEST_CASE("Button needs an action", "[required-callback]") {
    CHECK_THROWS_AS(Button(Text("Save"), {}), UsageError);
    CHECK_THROWS_AS(Button("Save", {}), UsageError);
    CHECK_NOTHROW(Button(Text("Save"), [] {}));
    CHECK_NOTHROW(Button("Save", [] {}));
}

TEST_CASE("floating, popover and tooltip need content", "[required-callback]") {
    const auto shown = Binding<bool>::constant(true);
    CHECK_THROWS_AS(Rectangle().floating(shown, {}), UsageError);
    CHECK_THROWS_AS(Rectangle().popover(shown, {}), UsageError);
    CHECK_THROWS_AS(Rectangle().tooltip(std::function<View()>()), UsageError);
    CHECK_NOTHROW(Rectangle().floating(shown, [] { return Text("x"); }));
    CHECK_NOTHROW(Rectangle().popover(shown, [] { return Text("x"); }));
    CHECK_NOTHROW(Rectangle().tooltip([] { return Text("x"); }));
}

TEST_CASE("on_click and on_double_click take an empty action, with or without the event", "[required-callback]") {
    CHECK_NOTHROW(Text("x").on_click(std::function<void()>()));
    CHECK_NOTHROW(Text("x").on_double_click(std::function<void()>()));
    CHECK_NOTHROW(Text("x").on_click(std::function<void(const ClickEvent&)>()));
    CHECK_NOTHROW(Text("x").on_click([] {}));
    CHECK_NOTHROW(Text("x").on_double_click([] {}));
}

TEST_CASE("menus need a menu function", "[required-callback]") {
    CHECK_THROWS_AS(Text("x").context_menu({}), UsageError);
    CHECK_THROWS_AS(Submenu("More", {}), UsageError);
    CHECK_THROWS_AS(MenuButton(Text("File"), {}), UsageError);
    CHECK_THROWS_AS(MenuButton("File", {}), UsageError);
    const auto menu = [] { return Menu(MenuItem("Open", [] {})); };
    CHECK_NOTHROW(Text("x").context_menu(menu));
    CHECK_NOTHROW(Submenu("More", menu));
    CHECK_NOTHROW(MenuButton(Text("File"), menu));
    CHECK_NOTHROW(MenuButton("File", menu));
}

TEST_CASE("gestures' on_began takes an empty action, with or without the event", "[required-callback]") {
    CHECK_NOTHROW(DragGesture().on_began(std::function<void()>()));
    CHECK_NOTHROW(MagnifyGesture().on_began(std::function<void()>()));
    CHECK_NOTHROW(RotateGesture().on_began(std::function<void()>()));
    CHECK_NOTHROW(DragGesture().on_began([] {}));
    CHECK_NOTHROW(MagnifyGesture().on_began([] {}));
    CHECK_NOTHROW(RotateGesture().on_began([] {}));
}

TEST_CASE("Binding needs a getter and a setter", "[required-callback]") {
    CHECK_THROWS_AS(Binding<int>(std::function<int()>(), [](int) {}), UsageError);
    CHECK_THROWS_AS(Binding<int>([] { return 1; }, std::function<void(int)>()), UsageError);
    CHECK_NOTHROW(Binding<int>([] { return 1; }, [](int) {}));
}

TEST_CASE("Computed needs a function", "[required-callback]") {
    CHECK_THROWS_AS(Computed<int>(std::function<int()>()), UsageError);
    CHECK_THROWS_AS(computed(std::function<int()>()), UsageError);
    int (*none)() = nullptr;
    CHECK_THROWS_AS(computed(none), UsageError);
    CHECK_NOTHROW(Computed<int>([] { return 1; }));
    CHECK(computed([] { return 2; })() == 2);
}

TEST_CASE("Signal needs a function", "[required-callback]") {
    CHECK_THROWS_AS(Signal<int>(std::function<int()>()), UsageError);
    const State<int> count(3);
    CHECK(Signal<int>(count)() == 3);
    CHECK(Signal<int>([] { return 4; })() == 4);
}

TEST_CASE("a reactive Prop needs a function", "[required-callback]") {
    CHECK_THROWS_AS(Prop<float>(std::function<float()>()), UsageError);
    CHECK_THROWS_AS(Prop<float>(std::function<float(const EnvironmentValues&)>()), UsageError);
    float (*none)() = nullptr;
    CHECK_THROWS_AS(Prop<float>(none), UsageError);
    CHECK_THROWS_AS(Text("x").opacity(std::function<float()>()), UsageError);
    CHECK(Prop<float>([] { return 0.5f; }).get() == 0.5f);
    CHECK(Prop<float>([](const EnvironmentValues&) { return 0.5f; }).get(EnvironmentValues()) == 0.5f);
}

TEST_CASE("effect needs a function", "[required-callback]") {
    const Owner owner;
    CHECK_THROWS_AS(effect(std::function<void()>()), UsageError);
    int runs = 0;
    effect([&] { ++runs; });
    CHECK(runs == 1);
}

TEST_CASE("ForEach needs items, a key function and a content function", "[required-callback]") {
    const std::vector<int> values{1, 2};
    CHECK_THROWS_AS(ForEach(std::function<std::vector<int>()>(), Row), UsageError);
    CHECK_THROWS_AS(ForEach(values, std::function<int(const int&)>(), Row), UsageError);
    CHECK_THROWS_AS(ForEach(std::vector<Item>(), static_cast<int Item::*>(nullptr), [](const Item&) { return View(); }),
                    UsageError);
    CHECK_THROWS_AS(ForEach(values, std::function<View(const int&)>()), UsageError);
    CHECK_THROWS_AS(ForEach(values, [](int value) { return value; }, std::function<View(const int&)>()), UsageError);
    CHECK_NOTHROW(ForEach(values, Row));
    CHECK_NOTHROW(ForEach([] { return std::vector<int>{1, 2}; }, Row));
    CHECK_NOTHROW(ForEach(std::vector<Item>(), &Item::id, [](const Item&) { return View(); }));
}

TEST_CASE("NavigationStack needs a destination function", "[required-callback]") {
    const State<std::vector<int>> path;
    CHECK_THROWS_AS(NavigationStack(path, Text("Root"), {}), UsageError);
    CHECK_THROWS_AS(NavigationStack(path.binding(), Text("Root"), {}), UsageError);
    CHECK_NOTHROW(NavigationStack(path, Text("Root"), Row));
    CHECK_NOTHROW(NavigationStack(path.binding(), Text("Root"), Row));
}

TEST_CASE("animation() needs a value to watch", "[required-callback]") {
    CHECK_THROWS_AS(Text("x").animation(Animation::smooth(), std::function<int()>()), UsageError);
    CHECK_NOTHROW(Text("x").animation(Animation::smooth(), [] { return 1; }));
    CHECK_NOTHROW(Text("x").animation(Animation::smooth(), 1));
}

TEST_CASE("post_to_ui_thread needs a task", "[required-callback]") {
    CHECK_THROWS_AS(post_to_ui_thread({}), UsageError);
}
