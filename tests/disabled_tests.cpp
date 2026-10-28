// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// .disabled() switches off a view's own event handlers: presses pass through to the views around
// it, gestures and keys stop, and what was going on when it became disabled is cancelled. Hover
// and scrolling go on.

#include "text/text_system.h"
#include "view/drop.h"
#include "view/event_dispatcher.h"
#include "view/focus.h"
#include "view/view_node.h"

#include <elg/events.h>
#include <elg/gestures.h>
#include <elg/shapes.h>
#include <elg/views.h>

#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>
#include <vector>

using namespace elg;

namespace {

detail::TextSystem& text_system() {
    static detail::TextSystem system;
    return system;
}

// A 100 x 100 window with `view` at (25, 25), 50 x 50.
struct Window {
    std::unique_ptr<detail::Element> root;
    detail::FocusManager focus;
    std::optional<detail::EventDispatcher> events;

    explicit Window(const View& view) {
        root = detail::lower(ZStack(Rectangle().fill(Color::white), view.frame(50, 50)), {.text = &text_system(), .focus = &focus});
        root->size_that_fits({100, 100});
        root->place({}, {100, 100});
        events.emplace(*root, focus);
    }

    void down(Point at) { events->pointer_button(PointerEvent::Kind::down, at, MouseButton::left, 1, KeyModifiers::none); }
    void up(Point at) { events->pointer_button(PointerEvent::Kind::up, at, MouseButton::left, 1, KeyModifiers::none); }
    void move(Point at) { events->pointer_moved(at); }
    void click(Point at) {
        move(at);
        down(at);
        up(at);
    }
    void press(Key key) {
        events->key({.kind = KeyEvent::Kind::down, .key = key});
        events->key({.kind = KeyEvent::Kind::up, .key = key});
    }
    // What the app does each frame.
    void frame() {
        detail::flush_effects();
        events->release_disabled();
        detail::flush_effects();
    }
};

const View kSquare = Rectangle().fill(Color::black);
constexpr Point kInside{50, 50};
constexpr Point kOutside{5, 5};

} // namespace

TEST_CASE("a disabled view's clicks pass to the views around it; hover goes on", "[disabled]") {
    detail::Scope scope;
    detail::Scope::Use use(scope);
    const State<bool> off(false);
    int inner = 0;
    int outer = 0;
    std::vector<bool> hovers;
    Window window(kSquare.on_click([&] { ++inner; })
                      .on_hover_change([&](bool inside) { hovers.push_back(inside); })
                      .hover_delay({.enter = 0, .exit = 0})
                      .disabled([off] { return off(); })
                      .on_click([&] { ++outer; }));
    window.click(kInside);
    CHECK(inner == 1);
    CHECK(outer == 0);

    off = true;
    window.frame();
    window.move(kOutside);
    window.events->tick();
    window.click(kInside);
    window.events->tick();
    CHECK(inner == 1);
    CHECK(outer == 1);
    CHECK(hovers == std::vector<bool>{true, false, true});

    off = false;
    window.frame();
    window.click(kInside);
    CHECK(inner == 2);
    CHECK(outer == 1);
}

TEST_CASE("disabling a view cancels its drag; a disabled view doesn't start one", "[disabled][gesture]") {
    detail::Scope scope;
    detail::Scope::Use use(scope);
    const State<bool> off(false);
    std::vector<std::string> calls;
    Window window(kSquare
                      .recognize(DragGesture()
                                     .on_began([&] { calls.emplace_back("began"); })
                                     .on_ended([&](const DragEvent&) { calls.emplace_back("ended"); })
                                     .on_cancelled([&] { calls.emplace_back("cancelled"); }))
                      .disabled([off] { return off(); }));
    window.move(kInside);
    window.down(kInside);
    window.move({70, 50});
    REQUIRE(calls == std::vector<std::string>{"began"});

    off = true;
    window.frame();
    CHECK(calls == std::vector<std::string>{"began", "cancelled"});
    window.move({80, 50});
    window.up({80, 50});
    CHECK(calls.size() == 2);

    window.move(kInside);
    window.down(kInside);
    window.move({70, 50});
    window.up({70, 50});
    CHECK(calls.size() == 2);
}

TEST_CASE("a disabled view takes no keys or focus, and focus leaves it", "[disabled][focus]") {
    detail::Scope scope;
    detail::Scope::Use use(scope);
    const State<bool> off(false);
    int keys = 0;
    Window window(kSquare.focusable().on_key_press(Key::space, [&] { ++keys; }).disabled([off] { return off(); }));
    window.press(Key::tab);
    REQUIRE(window.focus.focused());
    window.press(Key::space);
    CHECK(keys == 1);

    off = true;
    window.frame();
    CHECK_FALSE(window.focus.focused());
    window.press(Key::tab);
    CHECK_FALSE(window.focus.focused());
    window.click(kInside);
    CHECK_FALSE(window.focus.focused());
}

TEST_CASE("a focused control that becomes disabled loses focus", "[disabled][focus]") {
    detail::Scope scope;
    detail::Scope::Use use(scope);
    const State<bool> off(false);
    Window window(Button("OK", [] {}).disabled([off] { return off(); }));
    window.press(Key::tab);
    REQUIRE(window.focus.focused());
    off = true;
    window.frame();
    CHECK_FALSE(window.focus.focused());
}

TEST_CASE("a disabled drop target refuses; one around it may take the drop", "[disabled][drop]") {
    detail::Scope scope;
    detail::Scope::Use use(scope);
    std::string taker;
    const auto take = [&](std::string name) {
        return [&taker, name](const std::string&, Point) {
            taker = name;
            return true;
        };
    };
    Window window(kSquare.on_drop({.text = take("inner")}).disabled().on_drop({.text = take("outer")}));
    detail::DropElement* target = detail::find_drop_target(*window.root, kInside, false);
    REQUIRE(target);
    target->options().text("dropped", {});
    CHECK(taker == "outer");
}

TEST_CASE("a disabled window drag area still moves the window", "[disabled]") {
    detail::Scope scope;
    detail::Scope::Use use(scope);
    Window window(kSquare.window_drag_area().disabled());
    CHECK(window.events->drags_window_at(kInside));
}
