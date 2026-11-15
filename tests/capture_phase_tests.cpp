// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// The capture phase: capture handlers from the outside in, before bubble handlers from the
// inside out, for presses, scrolling, keys and text.

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

// A 100 x 100 window filled with `view`.
struct Window {
    detail::FocusManager focus; // outlives the tree: its elements let go of it when destroyed
    std::unique_ptr<detail::Element> root;
    std::unique_ptr<detail::EventDispatcher> events;

    explicit Window(const View& view) {
        root = detail::lower(view.frame(100, 100), {.text = &text_system(), .focus = &focus});
        root->size_that_fits({100.f, 100.f});
        root->place({}, {100.f, 100.f});
        events = std::make_unique<detail::EventDispatcher>(*root, focus);
    }

    void press(PointerEvent::Kind kind) const {
        events->pointer_button(kind, {50, 50}, MouseButton::left, 1, KeyModifiers::none);
    }
};

// A handler that logs `name` for each `down` and `up` and consumes the down if `takes`.
std::function<bool(const PointerEvent&)> logger(std::vector<std::string>& log, std::string name, bool takes = false) {
    return [&log, name = std::move(name), takes](const PointerEvent& event) {
        if (event.kind == PointerEvent::Kind::down) {
            log.push_back(name + " down");
            return takes;
        }
        if (event.kind == PointerEvent::Kind::up) {
            log.push_back(name + " up");
        }
        return false;
    };
}

} // namespace

TEST_CASE("a press goes through capture handlers outside in, then bubble handlers inside out", "[capture-phase]") {
    detail::Scope scope;
    const detail::Scope::Use use(scope);
    std::vector<std::string> log;
    const Window window(Rectangle()
                            .fill(Color::black)
                            .on_pointer(logger(log, "inner bubble"))
                            .on_pointer_capture(logger(log, "inner capture"))
                            .padding(10)
                            .on_pointer(logger(log, "outer bubble"))
                            .on_pointer_capture(logger(log, "outer capture")));
    window.press(PointerEvent::Kind::down);
    CHECK(log == std::vector<std::string>{"outer capture down", "inner capture down", "inner bubble down",
                                          "outer bubble down"});
}

TEST_CASE("a capture handler that takes a press keeps it from the views inside, and captures it", "[capture-phase]") {
    detail::Scope scope;
    const detail::Scope::Use use(scope);
    std::vector<std::string> log;
    const Window window(Rectangle()
                            .fill(Color::black)
                            .on_pointer(logger(log, "inner", true))
                            .padding(10)
                            .on_pointer_capture(logger(log, "outer", true)));
    window.press(PointerEvent::Kind::down);
    window.press(PointerEvent::Kind::up);
    CHECK(log == std::vector<std::string>{"outer down", "outer up"});
}

TEST_CASE("keys and text reach capture handlers around the focused view first", "[capture-phase]") {
    detail::Scope scope;
    const detail::Scope::Use use(scope);
    std::vector<std::string> log;
    const Window window(Rectangle()
                            .fill(Color::black)
                            .on_text_input([&](const TextInputEvent&) {
                                log.emplace_back("field text");
                                return true;
                            })
                            .on_key_press(Key::a, [&] { log.emplace_back("field key"); })
                            .padding(10)
                            .on_text_input_capture([&](const TextInputEvent&) {
                                log.emplace_back("outer text");
                                return false;
                            })
                            .on_key_press_capture(Key::a, [&] { log.emplace_back("outer key"); }));
    window.events->key({.kind = KeyEvent::Kind::down, .key = Key::tab}); // focus the field
    REQUIRE(window.focus.focused());
    window.events->key({.kind = KeyEvent::Kind::down, .key = Key::a});
    window.events->text_input({.text = "a"});
    CHECK(log == std::vector<std::string>{"outer key", "outer text", "field text"});
}

TEST_CASE("scrolling reaches capture handlers first", "[capture-phase]") {
    detail::Scope scope;
    const detail::Scope::Use use(scope);
    std::vector<std::string> log;
    const Window window(Rectangle()
                            .fill(Color::black)
                            .on_scroll([&](const ScrollEvent&) {
                                log.emplace_back("inner");
                                return true;
                            })
                            .padding(10)
                            .on_scroll_capture([&](const ScrollEvent&) {
                                log.emplace_back("outer");
                                return false;
                            }));
    window.events->scroll({.location = {50, 50}, .dy = 10});
    CHECK(log == std::vector<std::string>{"outer", "inner"});
}
