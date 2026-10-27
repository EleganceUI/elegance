// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Floating content: where it goes by its anchor, and in-window floating content (drawn by the
// window's floating layer) opening, closing, and taking presses.

#include "view/event_dispatcher.h"
#include "view/floating.h"
#include "view/focus.h"
#include "view/view_node.h"

#include <elg/events.h>
#include <elg/shapes.h>
#include <elg/error.h>
#include <elg/reactive.h>
#include <elg/styles.h>
#include <elg/views.h>

#include <catch2/catch_test_macros.hpp>

#include <optional>

using namespace elg;
using detail::place_floating;

namespace {

const Rect kArea{0, 0, 400, 300};
const Rect kAnchor{100, 100, 50, 20};

bool same(Point a, Point b) {
    return a.x == b.x && a.y == b.y;
}

// A 400 x 300 window whose root is a floating layer, as the app makes it.
struct Window {
    std::unique_ptr<detail::Element> root;
    detail::FocusManager focus;
    std::optional<detail::EventDispatcher> events;
    bool tree_changed = false;

    explicit Window(const View& view) {
        root = std::make_unique<detail::FloatingLayerElement>(
            detail::lower(ZStack({.alignment = Alignment::top_leading}, Rectangle().fill(Color::white), view),
                          {.tree_changed = &tree_changed, .focus = &focus}));
        layout();
        events.emplace(*root, focus);
    }

    // As the app does each frame: effects (bindings changing), then layout.
    void layout() {
        detail::flush_effects();
        root->size_that_fits({400, 300});
        root->place({}, {400, 300});
        if (events && std::exchange(tree_changed, false)) {
            events->revalidate();
        }
    }

    void click(Point at) {
        events->pointer_moved(at);
        events->pointer_button(PointerEvent::Kind::down, at, MouseButton::left, 1, KeyModifiers::none);
        events->pointer_button(PointerEvent::Kind::up, at, MouseButton::left, 1, KeyModifiers::none);
        layout();
    }
};

// A 50 x 20 button at (100, 100) with 80 x 40 floating content.
View Anchored(State<bool> shown, int& anchor_clicks, int& content_clicks, FloatingOptions options) {
    options.boundary = FloatingBoundary::window;
    return Rectangle()
        .fill(Color::black)
        .frame(50, 20)
        .on_click([&] { ++anchor_clicks; })
        .floating(shown.binding(),
                  [&] { return Rectangle().fill(Color::black).frame(80, 40).on_click([&] { ++content_clicks; }); },
                  options)
        .offset(100, 100);
}

} // namespace

TEST_CASE("floating content goes by its anchor's edge and alignment", "[floating]") {
    const Size size{80, 40};
    CHECK(same(place_floating(size, kAnchor, {}, kArea), {100, 120}));
    CHECK(same(place_floating(size, kAnchor, {.spacing = 4}, kArea), {100, 124}));
    CHECK(same(place_floating(size, kAnchor, {.alignment = FloatingAlignment::center}, kArea), {85, 120}));
    CHECK(same(place_floating(size, kAnchor, {.alignment = FloatingAlignment::end}, kArea), {70, 120}));
    CHECK(same(place_floating(size, kAnchor, {.edge = FloatingEdge::top, .spacing = 4}, kArea), {100, 56}));
    CHECK(same(place_floating(size, kAnchor, {.edge = FloatingEdge::trailing}, kArea), {150, 100}));
    CHECK(same(place_floating(size, kAnchor, {.edge = FloatingEdge::leading}, kArea), {20, 100}));
    CHECK(same(place_floating(size, kAnchor, {.offset = {3, 5}}, kArea), {103, 125}));
    CHECK(same(place_floating(size, kAnchor, {.position = Point{10, -30}}, kArea), {110, 70}));
}

TEST_CASE("floating content flips to fit, and stays inside its area", "[floating]") {
    const Size size{80, 40};
    // No room below: above instead.
    CHECK(same(place_floating(size, {100, 270, 50, 20}, {.spacing = 4}, kArea), {100, 226}));
    // No room on the right for start alignment: lined up with the anchor's end.
    CHECK(same(place_floating(size, {340, 100, 50, 20}, {}, kArea), {310, 120}));
    // A point near the bottom-right corner (a context menu): up and to the left.
    CHECK(same(place_floating(size, {390, 290, 0, 0}, {}, kArea), {310, 250}));
    // Too big to fit either way: clamped.
    CHECK(same(place_floating({500, 40}, kAnchor, {}, kArea), {0, 120}));
}

TEST_CASE("in-window floating content opens by its view and takes clicks", "[floating]") {
    detail::Scope scope;
    detail::Scope::Use use(scope);
    const State<bool> shown(false);
    int anchor_clicks = 0;
    int content_clicks = 0;
    Window window(Anchored(shown, anchor_clicks, content_clicks, {.placement = {.spacing = 4}}));

    window.click({110, 130}); // nothing there yet
    CHECK(content_clicks == 0);

    shown = true;
    window.layout();
    window.click({110, 130}); // the content: 100..180 x 124..164
    CHECK(content_clicks == 1);
    CHECK(shown.peek());

    shown = false;
    window.layout();
    window.click({110, 130});
    CHECK(content_clicks == 1);
}

TEST_CASE("a press outside in-window floating content closes it, and goes no further", "[floating]") {
    detail::Scope scope;
    detail::Scope::Use use(scope);
    const State<bool> shown(true);
    int anchor_clicks = 0;
    int content_clicks = 0;
    Window window(Anchored(shown, anchor_clicks, content_clicks, {}));
    window.layout();

    window.click({110, 105}); // the anchor, under the outside-click guard
    CHECK_FALSE(shown.peek());
    CHECK(anchor_clicks == 0);

    window.click({110, 105}); // closed: the anchor takes clicks again
    CHECK(anchor_clicks == 1);
}

TEST_CASE("in-window floating content that ignores outside clicks leaves the window usable", "[floating]") {
    detail::Scope scope;
    detail::Scope::Use use(scope);
    const State<bool> shown(true);
    int anchor_clicks = 0;
    int content_clicks = 0;
    Window window(Anchored(shown, anchor_clicks, content_clicks, {.dismiss = {.outside_click = false}}));
    window.layout();

    window.click({110, 105});
    CHECK(anchor_clicks == 1);
    CHECK(shown.peek());

    // Esc still closes it, with nothing focused.
    window.events->key({.kind = KeyEvent::Kind::down, .key = Key::escape});
    window.layout();
    CHECK_FALSE(shown.peek());
}

TEST_CASE("floating needs content", "[floating]") {
    CHECK_THROWS_AS(Rectangle().floating(Binding<bool>::constant(true), {}), UsageError);
}
