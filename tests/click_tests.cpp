// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// on_click reports where and how a view was clicked; on_activate also answers the keyboard and
// screen readers.

#include "view/accessibility.h"
#include "view/event_dispatcher.h"
#include "view/focus.h"
#include "view/view_node.h"

#include <elg/error.h>
#include <elg/events.h>
#include <elg/shapes.h>
#include <elg/styles.h>
#include <elg/views.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <memory>
#include <optional>
#include <vector>

using namespace elg;

namespace {

// A 100 x 100 window with `view` at (25, 25), 50 x 50.
struct Window {
    std::unique_ptr<detail::Element> root;
    detail::FocusManager focus;
    std::unique_ptr<detail::EventDispatcher> events;

    explicit Window(const View& view) {
        root = detail::lower(ZStack(Rectangle().fill(Color::white), view.frame(50, 50)), {});
        root->size_that_fits({100, 100});
        root->place({}, {100, 100});
        events = std::make_unique<detail::EventDispatcher>(*root, focus);
    }

    void click(Point at, MouseButton button = MouseButton::left, int count = 1,
               KeyModifiers modifiers = KeyModifiers::none) const {
        events->pointer_moved(at);
        events->pointer_button(PointerEvent::Kind::down, at, button, count, modifiers);
        events->pointer_button(PointerEvent::Kind::up, at, button, count, modifiers);
    }

    void press(Key key) const {
        events->key({.kind = KeyEvent::Kind::down, .key = key});
        events->key({.kind = KeyEvent::Kind::up, .key = key});
    }

    // Performs a screen reader's default action on the node that has one.
    [[nodiscard]] bool screen_reader_click() const {
        const detail::AccessTree tree = detail::build_accessibility_tree(*root, "Test", {100, 100}, nullptr);
        const auto found = std::ranges::find_if(tree.targets, [](const auto& entry) { return bool(entry.second.action); });
        if (found == tree.targets.end()) {
            return false;
        }
        found->second.action();
        return true;
    }
};

const View kSquare = Rectangle().fill(Color::black);

} // namespace

TEST_CASE("a click reports its location in the view, its button, modifiers and count", "[click]") {
    detail::Scope scope;
    const detail::Scope::Use use(scope);
    std::optional<ClickEvent> seen;
    const Window window(kSquare.on_click([&](const ClickEvent& click) { seen = click; }));

    window.click({30, 40}, MouseButton::left, 1, KeyModifiers::shift);
    REQUIRE(seen);
    // NOLINTBEGIN(bugprone-unchecked-optional-access): REQUIRE(seen) stops the test without a click
    CHECK(seen->location.x == 5);
    CHECK(seen->location.y == 15);
    CHECK(seen->window_location.x == 30);
    CHECK(seen->button == MouseButton::left);
    CHECK(seen->modifiers == KeyModifiers::shift);
    CHECK(seen->click_count == 1);
    // NOLINTEND(bugprone-unchecked-optional-access)

    seen.reset();
    window.click({90, 90}); // outside
    CHECK_FALSE(seen);
}

TEST_CASE("a click handler only takes its button", "[click]") {
    detail::Scope scope;
    const detail::Scope::Use use(scope);
    int left = 0;
    int right = 0;
    const Window window(kSquare.on_click([&] { ++left; }).on_click([&] { ++right; }, {.button = MouseButton::right}));

    window.click({50, 50}, MouseButton::right);
    CHECK(right == 1);
    CHECK(left == 0);
    window.click({50, 50});
    CHECK(left == 1);
    CHECK(right == 1);
}

TEST_CASE("a double-click handler lets the first click through to a single-click one", "[click]") {
    detail::Scope scope;
    const detail::Scope::Use use(scope);
    int singles = 0;
    int doubles = 0;
    const Window window(kSquare.on_double_click([&] { ++doubles; }).padding(0).on_click([&] { ++singles; }));

    window.click({50, 50}, MouseButton::left, 1);
    window.click({50, 50}, MouseButton::left, 2);
    CHECK(singles == 1);
    CHECK(doubles == 1);
}

TEST_CASE("on_activate runs on a click, on Space or Enter with focus, and for screen readers", "[click]") {
    detail::Scope scope;
    const detail::Scope::Use use(scope);
    int activated = 0;
    Window window(kSquare.on_activate([&] { ++activated; }));

    window.click({50, 50});
    CHECK(activated == 1);
    // The click focused it.
    window.press(Key::space);
    CHECK(activated == 2);
    window.press(Key::enter);
    CHECK(activated == 3);
    // Held keys don't repeat it.
    window.events->key({.kind = KeyEvent::Kind::down, .key = Key::space, .repeat = true});
    CHECK(activated == 3);

    CHECK(window.screen_reader_click());
    CHECK(activated == 4);
}

TEST_CASE("a disabled view doesn't activate", "[click]") {
    detail::Scope scope;
    const detail::Scope::Use use(scope);
    int activated = 0;
    const Window window(kSquare.on_activate([&] { ++activated; }).disabled(true));

    window.click({50, 50});
    window.press(Key::space);
    static_cast<void>(window.screen_reader_click()); // found or not, nothing may activate
    CHECK(activated == 0);
}

TEST_CASE("on_activate needs an action", "[click]") {
    CHECK_THROWS_AS(kSquare.on_activate({}), UsageError);
}
