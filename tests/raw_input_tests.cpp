// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// on_pointer, on_scroll, on_key and on_text_input: the events themselves, in the view's
// coordinates; returning true consumes them.

#include "view/event_dispatcher.h"
#include "view/focus.h"
#include "view/view_node.h"

#include <elg/events.h>
#include <elg/shapes.h>
#include <elg/styles.h>
#include <elg/video.h>
#include <elg/views.h>

#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>
#include <vector>

using namespace elg;

namespace {

// A 100 x 100 window with `view` at (25, 25), 50 x 50.
struct Window {
    std::unique_ptr<detail::Element> root;
    detail::FocusManager focus;
    std::optional<detail::EventDispatcher> events;

    explicit Window(const View& view) {
        root = detail::lower(ZStack(Rectangle().fill(Color::white), view.frame(50, 50)), {});
        root->size_that_fits({100, 100});
        root->place({}, {100, 100});
        events.emplace(*root, focus);
    }

    void button(PointerEvent::Kind kind, Point at) {
        events->pointer_button(kind, at, MouseButton::left, 1, KeyModifiers::none);
    }
};

const View kSquare = Rectangle().fill(Color::black);

} // namespace

TEST_CASE("on_pointer gets view-local events, and a consumed down captures the pointer", "[input]") {
    detail::Scope scope;
    detail::Scope::Use use(scope);
    std::vector<PointerEvent> seen;
    Window window(kSquare.on_pointer([&](const PointerEvent& event) {
        seen.push_back(event);
        return event.kind != PointerEvent::Kind::hover;
    }));

    window.events->pointer_moved({30, 40});
    window.button(PointerEvent::Kind::down, {30, 40});
    window.events->pointer_moved({95, 95}); // outside, but captured
    window.button(PointerEvent::Kind::up, {95, 95});

    REQUIRE(seen.size() == 4);
    CHECK(seen[0].kind == PointerEvent::Kind::hover);
    CHECK(seen[1].kind == PointerEvent::Kind::down);
    CHECK(seen[1].location.x == 5);
    CHECK(seen[1].location.y == 15);
    CHECK(seen[1].window_location.x == 30);
    CHECK(seen[2].kind == PointerEvent::Kind::move);
    CHECK(seen[2].location.x == 70);
    CHECK(seen[3].kind == PointerEvent::Kind::up);
}

TEST_CASE("on_scroll gets view-local scrolls; unconsumed ones go on outwards", "[input]") {
    detail::Scope scope;
    detail::Scope::Use use(scope);
    for (const bool take : {false, true}) {
        std::optional<ScrollEvent> inner;
        int outer = 0;
        Window window(kSquare
                          .on_scroll([&](const ScrollEvent& event) {
                              inner = event;
                              return take;
                          })
                          .padding(0)
                          .on_scroll([&](const ScrollEvent&) {
                              ++outer;
                              return true;
                          }));

        window.events->scroll({.location = {30, 40}, .dy = 3});
        REQUIRE(inner);
        CHECK(inner->location.x == 5);
        CHECK(inner->location.y == 15);
        CHECK(inner->window_location.y == 40);
        CHECK(inner->dy == 3);
        CHECK(outer == (take ? 0 : 1));
    }
}

TEST_CASE("a video view is hit over its frame, so its handlers get events", "[input]") {
    detail::Scope scope;
    detail::Scope::Use use(scope);
    int pointers = 0;
    int scrolls = 0;
    Window window(VideoFeedView(VideoFeed(), ContentMode::fill)
                      .on_pointer([&](const PointerEvent&) {
                          ++pointers;
                          return true;
                      })
                      .on_scroll([&](const ScrollEvent&) {
                          ++scrolls;
                          return true;
                      }));

    window.button(PointerEvent::Kind::down, {30, 40});
    window.button(PointerEvent::Kind::up, {30, 40});
    window.events->scroll({.location = {30, 40}, .dy = 3});
    CHECK(pointers == 2);
    CHECK(scrolls == 1);
}

TEST_CASE("on_key sees downs and ups; on_key_press only downs", "[input]") {
    detail::Scope scope;
    detail::Scope::Use use(scope);
    std::vector<KeyEvent::Kind> keys;
    int presses = 0;
    Window window(kSquare.focusable().on_key_press(Key::a, [&] { ++presses; }).on_key([&](const KeyEvent& event) {
        keys.push_back(event.kind);
        return false;
    }));

    window.button(PointerEvent::Kind::down, {50, 50}); // focuses it
    window.button(PointerEvent::Kind::up, {50, 50});
    window.events->key({.kind = KeyEvent::Kind::down, .key = Key::b});
    window.events->key({.kind = KeyEvent::Kind::up, .key = Key::b});
    window.events->key({.kind = KeyEvent::Kind::down, .key = Key::a});
    window.events->key({.kind = KeyEvent::Kind::up, .key = Key::a});

    CHECK(keys == std::vector{KeyEvent::Kind::down, KeyEvent::Kind::up, KeyEvent::Kind::up});
    CHECK(presses == 1);
}

TEST_CASE("on_text_input makes the view focusable and gets what's typed", "[input]") {
    detail::Scope scope;
    detail::Scope::Use use(scope);
    std::string typed;
    Window window(kSquare.on_text_input([&](const TextInputEvent& event) {
        if (event.kind == TextInputEvent::Kind::commit) {
            typed += event.text;
        }
        return true;
    }));

    window.events->text_input({.text = "lost"}); // nothing has focus yet
    window.button(PointerEvent::Kind::down, {50, 50});
    window.button(PointerEvent::Kind::up, {50, 50});
    window.events->text_input({.text = "hé"});
    window.events->text_input({.kind = TextInputEvent::Kind::composition, .text = "k"});
    CHECK(typed == "hé");
}
