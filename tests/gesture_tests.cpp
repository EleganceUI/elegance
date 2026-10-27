// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Gestures watch presses and take them over once they recognize them; what had the press is
// cancelled.

#include "view/event_dispatcher.h"
#include "view/focus.h"
#include "view/view_node.h"

#include <elg/events.h>
#include <elg/gestures.h>
#include <elg/shapes.h>
#include <elg/styles.h>
#include <elg/views.h>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <optional>
#include <string>
#include <thread>
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

    void down(Point at) { events->pointer_button(PointerEvent::Kind::down, at, MouseButton::left, 1, KeyModifiers::none); }
    void up(Point at) { events->pointer_button(PointerEvent::Kind::up, at, MouseButton::left, 1, KeyModifiers::none); }
    void move(Point at) { events->pointer_moved(at); }
    void click(Point at) {
        move(at);
        down(at);
        up(at);
    }
};

const View kSquare = Rectangle().fill(Color::black);

} // namespace

TEST_CASE("a drag begins after its minimum distance and reports where it went", "[gesture]") {
    detail::Scope scope;
    detail::Scope::Use use(scope);
    std::vector<std::string> calls;
    std::optional<DragEvent> last;
    Window window(kSquare.recognize(DragGesture({.minimum_distance = 5})
                                        .on_began([&] { calls.emplace_back("began"); })
                                        .on_changed([&](const DragEvent& e) {
                                            calls.emplace_back("changed");
                                            last = e;
                                        })
                                        .on_ended([&](const DragEvent& e) {
                                            calls.emplace_back("ended");
                                            last = e;
                                        })));

    window.move({30, 30});
    window.down({30, 30});
    window.move({32, 31}); // not far enough yet
    CHECK(calls.empty());
    window.move({40, 30});
    window.move({90, 90}); // outside the view: still the drag's
    window.up({90, 90});

    CHECK(calls == std::vector<std::string>{"began", "changed", "changed", "ended"});
    REQUIRE(last);
    CHECK(last->start_location.x == 5);
    CHECK(last->start_location.y == 5);
    CHECK(last->location.x == 65);
    CHECK(last->translation.x == 60);
    CHECK(last->translation.y == 60);
}

TEST_CASE("a drag with no minimum distance begins on the press", "[gesture]") {
    detail::Scope scope;
    detail::Scope::Use use(scope);
    int began = 0;
    Window window(kSquare.recognize(DragGesture({.minimum_distance = 0}).on_began([&] { ++began; })));
    window.move({30, 30});
    window.down({30, 30});
    CHECK(began == 1);
}

TEST_CASE("a drag that starts on a button or a click handler cancels the click", "[gesture]") {
    detail::Scope scope;
    detail::Scope::Use use(scope);
    int pressed = 0;
    int clicked = 0;
    int dragged = 0;
    Window window(VStack(Button(Rectangle().fill(Color::black).frame(50, 25), [&] { ++pressed; })
                             .button_style(PlainButtonStyle{}),
                         Rectangle().fill(Color::black).frame(50, 25))
                      .on_click([&] { ++clicked; })
                      .on_drag([&](const DragEvent&) { ++dragged; }));

    // Clicks still work.
    window.click({50, 30}); // the button
    window.click({50, 65}); // the rest of the card
    CHECK(pressed == 1);
    CHECK(clicked == 1);

    // Drags don't click.
    for (const Point start : {Point{50, 30}, Point{50, 65}}) {
        window.move(start);
        window.down(start);
        window.move({start.x + 20, start.y});
        window.up({start.x + 20, start.y});
    }
    CHECK(pressed == 1);
    CHECK(clicked == 1);
    CHECK(dragged == 2);
}

TEST_CASE("a handler that keeps its press isn't taken over by gestures around it", "[gesture]") {
    detail::Scope scope;
    detail::Scope::Use use(scope);
    std::vector<PointerEvent::Kind> tracked;
    int dragged = 0;
    // Like a slider: tracks the pointer itself.
    const auto track = [&](const PointerEvent& e) {
        tracked.push_back(e.kind);
        return e.kind != PointerEvent::Kind::hover;
    };

    SECTION("around it: the handler keeps the press") {
        Window window(kSquare.on_pointer(track).on_drag([&](const DragEvent&) { ++dragged; }));
        window.move({30, 30});
        window.down({30, 30});
        window.move({60, 30});
        window.up({60, 30});
        CHECK(dragged == 0);
        CHECK(tracked.back() == PointerEvent::Kind::up);
    }
    SECTION("inside it: the gesture wins") {
        Window window(kSquare.on_drag([&](const DragEvent&) { ++dragged; }).on_pointer(track));
        window.move({30, 30});
        window.down({30, 30});
        window.move({60, 30});
        window.up({60, 30});
        CHECK(dragged == 1);
        CHECK(tracked.back() == PointerEvent::Kind::cancel);
    }
}

TEST_CASE("a long press fires once held, and the click doesn't", "[gesture]") {
    detail::Scope scope;
    detail::Scope::Use use(scope);
    int long_pressed = 0;
    int clicked = 0;
    std::vector<bool> pressing;
    Window window(kSquare
                      .recognize(LongPressGesture([&] { ++long_pressed; },
                                                  {.duration = 0.02, .pressing = [&](bool p) { pressing.push_back(p); }}))
                      .on_click([&] { ++clicked; }));

    window.move({50, 50});
    window.down({50, 50});
    CHECK(window.events->next_deadline());
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    window.events->tick();
    CHECK(long_pressed == 1);
    window.up({50, 50});
    CHECK(clicked == 0);
    CHECK(pressing == std::vector{true, false});

    // Released early: a click.
    window.click({50, 50});
    CHECK(long_pressed == 1);
    CHECK(clicked == 1);

    // Wandered off: neither.
    window.down({50, 50});
    window.move({50, 75});
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    window.events->tick();
    window.up({50, 75});
    CHECK(long_pressed == 1);
}

TEST_CASE("a long press that came due fires on the release even before a tick", "[gesture]") {
    detail::Scope scope;
    detail::Scope::Use use(scope);
    int long_pressed = 0;
    int clicked = 0;
    Window window(kSquare.recognize(LongPressGesture([&] { ++long_pressed; }, {.duration = 0.01})).on_click([&] { ++clicked; }));
    window.move({50, 50});
    window.down({50, 50});
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    window.up({50, 50});
    CHECK(long_pressed == 1);
    CHECK(clicked == 0);
}

TEST_CASE("trackpad gestures call their phases' callbacks", "[gesture]") {
    detail::Scope scope;
    detail::Scope::Use use(scope);
    std::vector<std::string> calls;
    Window window(kSquare.recognize(MagnifyGesture()
                                        .on_began([&] { calls.emplace_back("began"); })
                                        .on_changed([&](const MagnifyEvent& e) { calls.push_back(std::to_string(e.scale)); })
                                        .on_ended([&](const MagnifyEvent&) { calls.emplace_back("ended"); })));
    window.move({50, 50});
    window.events->magnify({.location = {50, 50}, .phase = GesturePhase::began});
    window.events->magnify({.location = {50, 50}, .scale = 2, .delta = 2, .phase = GesturePhase::changed});
    window.events->magnify({.location = {50, 50}, .scale = 2, .delta = 1, .phase = GesturePhase::ended});
    CHECK(calls == std::vector<std::string>{"began", std::to_string(2.f), "ended"});
}
