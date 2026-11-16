// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Tab and Shift+Tab: the order of focus stops, `.focused()` scopes in it, and the cost of a press.

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

// A focus stop that logs its name when it gets focus, and counts how often it's asked whether
// it's focusable.
class StopHandler final : public EventHandler {
public:
    StopHandler(std::string name, std::vector<std::string>* log, int* asked)
        : name_(std::move(name)), log_(log), asked_(asked) {}
    [[nodiscard]] bool focusable() const override {
        ++*asked_;
        return true;
    }
    EventResult focus(bool focused) override {
        if (focused) {
            log_->push_back(name_);
        }
        return {};
    }

private:
    std::string name_;
    std::vector<std::string>* log_;
    int* asked_;
};

class Stop final : public EventModifier {
public:
    Stop(std::string name, std::vector<std::string>* log, int* asked)
        : name_(std::move(name)), log_(log), asked_(asked) {}
    [[nodiscard]] std::unique_ptr<EventHandler> make_handler() const override {
        return std::make_unique<StopHandler>(name_, log_, asked_);
    }

private:
    std::string name_;
    std::vector<std::string>* log_;
    int* asked_;
};

struct Window {
    detail::FocusManager focus; // outlives the tree: its elements let go of it when destroyed
    std::unique_ptr<detail::Element> root;
    std::unique_ptr<detail::EventDispatcher> events;

    explicit Window(const View& view) {
        root = detail::lower(view, {.text = &text_system(), .focus = &focus});
        root->size_that_fits({400.f, 400.f});
        root->place({}, {400.f, 400.f});
        events = std::make_unique<detail::EventDispatcher>(*root, focus);
    }

    void press(Key key, KeyModifiers modifiers = KeyModifiers::none) const {
        events->key({.kind = KeyEvent::Kind::down, .key = key, .modifiers = modifiers});
        events->key({.kind = KeyEvent::Kind::up, .key = key, .modifiers = modifiers});
        detail::flush_effects();
    }
};

View square() {
    return Rectangle().fill(Color::black).frame(20, 20);
}

} // namespace

TEST_CASE("Tab goes through the stops in tree order, scopes included, and wraps", "[focus][tab]") {
    detail::Scope scope;
    const detail::Scope::Use use(scope);
    std::vector<std::string> log;
    int asked = 0;
    const State<bool> empty_scope(false);
    const State<bool> field_scope(false);
    const Window window(HStack(square().modifier(Stop("a", &log, &asked)),
                               square().focused(empty_scope.binding()), // nothing inside: a stop itself
                               square().modifier(Stop("b", &log, &asked)).focused(field_scope.binding()),
                               square().modifier(Stop("c", &log, &asked))));
    window.press(Key::tab);
    CHECK(log == std::vector<std::string>{"a"});
    window.press(Key::tab);
    CHECK(empty_scope());
    window.press(Key::tab);
    CHECK(log == std::vector<std::string>{"a", "b"}); // the field, not the scope around it
    CHECK(field_scope());
    window.press(Key::tab);
    window.press(Key::tab); // wraps around
    CHECK(log == std::vector<std::string>{"a", "b", "c", "a"});
    window.press(Key::tab, KeyModifiers::shift); // and back
    CHECK(log.back() == "c");
}

TEST_CASE("one Tab press asks each element once, however deep the focus scopes nest", "[focus][tab]") {
    detail::Scope scope;
    const detail::Scope::Use use(scope);
    std::vector<std::string> log;
    int asked = 0;
    const std::vector<State<bool>> scopes(24, State<bool>(false));
    View view = square().modifier(Stop("inner", &log, &asked));
    for (const State<bool>& focused : scopes) {
        view = view.focused(focused.binding());
    }
    const Window window(view);
    asked = 0;
    window.press(Key::tab);
    CHECK(log == std::vector<std::string>{"inner"});
    CHECK(asked <= 2); // walking the scopes one by one asked it once per scope, and per scope above
}

TEST_CASE("a press focuses the innermost focusable view under it, or clears focus", "[focus]") {
    detail::Scope scope;
    const detail::Scope::Use use(scope);
    std::vector<std::string> log;
    int asked = 0;
    const Window window(HStack({.spacing = 0}, square().modifier(Stop("a", &log, &asked)), square(),
                               square().modifier(Stop("c", &log, &asked)).focusable(false)));
    const auto press = [&](Point at) {
        window.events->pointer_button(PointerEvent::Kind::down, at, MouseButton::left, 1, KeyModifiers::none);
        window.events->pointer_button(PointerEvent::Kind::up, at, MouseButton::left, 1, KeyModifiers::none);
    };
    press({10, 10});
    CHECK(log == std::vector<std::string>{"a"});
    CHECK_FALSE(window.focus.ring_visible()); // focus from a press shows no ring
    press({30, 10}); // nothing focusable there
    CHECK_FALSE(window.focus.focused());
    window.press(Key::tab);
    CHECK(window.focus.ring_visible()); // focus from Tab does
    press({50, 10}); // inside .focusable(false): focus stays where it is
    CHECK(window.focus.focused());
}
