// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Views that go while the user is interacting with them: what they had going ends before they
// go, and nothing reaches them afterwards.

#include "text/text_system.h"
#include "view/event_dispatcher.h"
#include "view/focus.h"
#include "view/view_node.h"

#include <elg/elegance.h>

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <vector>

using namespace elg;

namespace {

detail::TextSystem& text_system() {
    static detail::TextSystem system;
    return system;
}

// A 100 x 100 window with `view` in it, which `shown` mounts and unmounts.
struct Window {
    detail::FocusManager focus; // outlives the tree: its elements let go of it when destroyed
    std::unique_ptr<detail::Element> root;
    std::unique_ptr<detail::EventDispatcher> events;
    bool tree_changed = false;

    Window(const State<bool>& shown, const View& view) {
        root = detail::lower(ZStack(Rectangle().fill(Color::white), If([shown] { return shown(); }, view.frame(50, 50))),
                             {.text = &text_system(), .tree_changed = &tree_changed, .focus = &focus});
        layout();
        events = std::make_unique<detail::EventDispatcher>(*root, focus);
    }

    // As the app does after each event: effects, then layout, then forgetting what went.
    void frame() {
        detail::flush_effects();
        layout();
        if (events && std::exchange(tree_changed, false)) {
            events->revalidate();
        }
    }

    void layout() const {
        root->size_that_fits({100.f, 100.f});
        root->place({}, {100.f, 100.f});
    }

    void button(PointerEvent::Kind kind) const {
        events->pointer_button(kind, {50, 50}, MouseButton::left, 1, KeyModifiers::none);
    }
};

} // namespace

TEST_CASE("a pressed view that goes hears its press is cancelled, and nothing after", "[leaving]") {
    detail::Scope scope;
    const detail::Scope::Use use(scope);
    const State<bool> shown(true);
    std::vector<std::string> log;
    Window window(shown, Rectangle().fill(Color::black).on_pointer([&](const PointerEvent& event) {
        if (event.kind == PointerEvent::Kind::down) {
            log.emplace_back("down");
            return true;
        }
        if (event.kind == PointerEvent::Kind::cancel) {
            log.emplace_back("cancel");
        }
        if (event.kind == PointerEvent::Kind::up) {
            log.emplace_back("up");
        }
        return false;
    }));
    window.events->pointer_moved({50, 50});
    window.button(PointerEvent::Kind::down);
    shown = false;
    window.frame();
    window.button(PointerEvent::Kind::up);
    CHECK(log == std::vector<std::string>{"down", "cancel"});
}

TEST_CASE("a hovered view that goes hears the pointer left", "[leaving]") {
    detail::Scope scope;
    const detail::Scope::Use use(scope);
    const State<bool> shown(true);
    std::vector<bool> hovered;
    Window window(shown, Rectangle().fill(Color::black).on_hover_change([&](bool inside) { hovered.push_back(inside); }));
    window.events->pointer_moved({50, 50});
    REQUIRE(hovered == std::vector<bool>{true});
    shown = false;
    window.frame();
    CHECK(hovered == std::vector<bool>{true, false});
}

TEST_CASE("a focused view that goes takes focus with it", "[leaving][focus]") {
    detail::Scope scope;
    const detail::Scope::Use use(scope);
    const State<bool> shown(true);
    Window window(shown, Rectangle().fill(Color::black).focusable());
    window.events->key({.kind = KeyEvent::Kind::down, .key = Key::tab});
    REQUIRE(window.focus.focused());
    shown = false;
    window.frame();
    CHECK_FALSE(window.focus.focused());
}
