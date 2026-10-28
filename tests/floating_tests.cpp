// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Floating content: where it goes by its anchor, and in-window floating content (drawn by the
// window's floating layer) opening, closing, and taking presses.

#include "render/display_list.h"
#include "text/text_system.h"
#include "view/animated.h"
#include "view/event_dispatcher.h"
#include "view/floating.h"
#include "view/focus.h"
#include "view/view_node.h"

#include <elg/events.h>
#include <elg/shapes.h>
#include <elg/animation.h>
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
                          {.text = &text_system(), .tree_changed = &tree_changed, .focus = &focus}));
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

    static detail::TextSystem& text_system() {
        static detail::TextSystem system;
        return system;
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

namespace {

int children(detail::Element& element) {
    int count = 0;
    element.for_each_child([&](detail::Element&) { ++count; });
    return count;
}

} // namespace

TEST_CASE("a popover's chrome comes from the environment's PopoverStyle", "[floating]") {
    detail::Scope scope;
    detail::Scope::Use use(scope);
    struct Recording final : PopoverStyle {
        int* built;
        bool* padded;
        Recording(int* b, bool* p) : built(b), padded(p) {}
        View make_body(const PopoverConfiguration& popover) const override {
            ++*built;
            *padded = popover.is_padded();
            return popover.content;
        }
    };
    int built = 0;
    bool padded = true;
    const State<bool> shown(true);
    Window window(Rectangle()
                      .frame(50, 20)
                      .popover(shown.binding(), [] { return Rectangle().frame(80, 40); },
                               {.boundary = FloatingBoundary::window, .padded = false})
                      .popover_style(Recording(&built, &padded)));
    window.layout();
    CHECK(built == 1);
    CHECK_FALSE(padded);
}

TEST_CASE("an in-window tooltip shows on hover, lets the pointer through, and hides on leaving", "[floating]") {
    detail::Scope scope;
    detail::Scope::Use use(scope);
    int built = 0;
    int clicks = 0;
    Window window(ZStack({.alignment = Alignment::top_leading},
                         Rectangle().fill(Color::black).frame(400, 300).on_click([&] { ++clicks; }),
                         Rectangle()
                             .fill(Color::black)
                             .frame(50, 20)
                             .tooltip(
                                 [&] {
                                     ++built;
                                     return Rectangle().fill(Color::black).frame(80, 40);
                                 },
                                 {.boundary = FloatingBoundary::window, .delay = 0})
                             .tooltip_style(PlainTooltipStyle{})));
    window.events->pointer_moved({10, 10});
    window.events->tick();
    window.layout();
    CHECK(built == 1);
    CHECK(children(*window.root) == 2); // the content and the tooltip

    window.click({20, 30}); // on the tooltip (0..80 x 24..64): reaches the view under it
    CHECK(clicks == 1);
    CHECK(children(*window.root) == 1); // the press hid it

    window.events->pointer_moved({10, 10});
    window.events->pointer_moved({300, 200});
    window.events->tick();
    window.layout();
    CHECK(children(*window.root) == 1);
}

TEST_CASE("a menu's panel comes from the environment's MenuStyle", "[floating]") {
    detail::Scope scope;
    detail::Scope::Use use(scope);
    int built = 0;
    struct Recording final : MenuStyle {
        int* built;
        explicit Recording(int* b) : built(b) {}
        View make_body(const MenuConfiguration& menu) const override {
            ++*built;
            return menu.items;
        }
    };
    Window window(Menu(MenuItem("Open", [] {})).menu_style(Recording(&built)));
    CHECK(built == 1);
}

namespace {

struct Painted {
    std::size_t commands = 0;
    bool animating = false;
};

Painted paint(detail::Element& root) {
    detail::DisplayList list;
    root.paint(list);
    return {list.commands.size(), list.animating};
}

} // namespace

TEST_CASE("a scale transition can grow from an edge", "[floating]") {
    const Transition::Pose top = Transition::scale(0.5f, Alignment::top_leading).pose(0, false, {100, 40});
    CHECK(top.scale == 0.5f);
    CHECK(top.anchor_x == 0);
    CHECK(top.anchor_y == 0);
    const Transition::Pose middle = Transition::opacity().combined(Transition::scale(0.5f)).pose(0, false, {100, 40});
    CHECK(middle.anchor_x == 0.5f);
    // Combined, the anchored one's point wins.
    const Transition::Pose bottom =
        Transition::opacity().combined(Transition::scale(0.9f, Alignment::bottom)).pose(0, true, {100, 40});
    CHECK(bottom.anchor_y == 1);
}

TEST_CASE("in-window floating content with a motion animates in, then out before it goes", "[floating]") {
    detail::Scope scope;
    detail::Scope::Use use(scope);
    detail::set_frame_time(100);
    const State<bool> shown(false);
    int anchor_clicks = 0;
    int content_clicks = 0;
    Window window(Anchored(shown, anchor_clicks, content_clicks,
                           {.motion = PopupMotion(Transition::opacity(), Animation::linear(1))}));
    const Painted closed = paint(*window.root);

    shown = true;
    window.layout();
    CHECK(paint(*window.root).animating);
    detail::set_frame_time(100.5); // halfway: drawn, faded
    const Painted opening = paint(*window.root);
    CHECK(opening.animating);
    CHECK(opening.commands > closed.commands);

    detail::set_frame_time(102);
    window.layout();
    CHECK_FALSE(paint(*window.root).animating);

    shown = false;
    window.layout();
    const Painted leaving = paint(*window.root);
    CHECK(leaving.animating);
    CHECK(leaving.commands > closed.commands); // still drawn
    CHECK(children(*window.root) == 1);         // but no longer part of the tree for events
    window.click({110, 105});                    // the anchor, no longer guarded
    CHECK(anchor_clicks == 1);

    detail::set_frame_time(104);
    window.layout();
    window.layout(); // pruned once finished
    CHECK(paint(*window.root).commands == closed.commands);
}

TEST_CASE("a popover takes its motion from its style unless its options give one", "[floating]") {
    detail::Scope scope;
    detail::Scope::Use use(scope);
    detail::set_frame_time(100);
    struct Fading final : PopoverStyle {
        View make_body(const PopoverConfiguration& popover) const override { return popover.content; }
        PopupMotion motion() const override { return {Transition::opacity(), Animation::linear(1)}; }
    };
    const State<bool> styled(true);
    const State<bool> overridden(true);
    Window window(VStack(Rectangle().frame(50, 20).popover(styled.binding(), [] { return Rectangle().frame(80, 40); },
                                                           {.boundary = FloatingBoundary::window}),
                         Rectangle().frame(50, 20).popover(overridden.binding(), [] { return Rectangle().frame(80, 40); },
                                                           {.boundary = FloatingBoundary::window,
                                                            .motion = PopupMotion::none()}))
                      .popover_style(Fading{}));
    window.layout();
    int transitions = 0;
    window.root->for_each_child([&](detail::Element& child) { transitions += child.as_transition() ? 1 : 0; });
    CHECK(transitions == 1);
}

TEST_CASE("a press on in-window floating content that nothing in it takes keeps it open", "[floating]") {
    detail::Scope scope;
    detail::Scope::Use use(scope);
    const State<bool> shown(true);
    Window window(Rectangle()
                      .fill(Color::black)
                      .frame(50, 20)
                      .floating(shown.binding(), [] { return Rectangle().fill(Color::black).frame(80, 40); },
                                {.boundary = FloatingBoundary::window})
                      .offset(100, 100));
    window.layout();
    window.click({110, 130}); // the content: 100..180 x 120..160
    CHECK(shown.peek());
    window.click({300, 250});
    CHECK_FALSE(shown.peek());
}

namespace {

// A 200 x 300 container on the left with a 50 x 20 anchor at (140, 100) inside it, and a
// 200 x 300 sibling on the right. The content is 80 x 40, to the anchor's trailing side.
struct ContainerScene {
    State<bool> shown{true};
    int content_clicks = 0;
    int sibling_clicks = 0;
    int container_clicks = 0;

    View view(bool marked) {
        View left = ZStack({.alignment = Alignment::top_leading},
                           Rectangle().fill(Color::white).frame(200, 300).on_click([this] { ++container_clicks; }),
                           Rectangle()
                               .fill(Color::black)
                               .frame(50, 20)
                               .floating(shown.binding(),
                                         [this] {
                                             return Rectangle().fill(Color::black).frame(80, 40).on_click(
                                                 [this] { ++content_clicks; });
                                         },
                                         {.placement = {.edge = FloatingEdge::trailing},
                                          .boundary = FloatingBoundary::container})
                               .offset(140, 100));
        if (marked) {
            left = left.floating_container();
        }
        return HStack({.spacing = 0}, left,
                      Rectangle().fill(Color::white).frame(200, 300).on_click([this] { ++sibling_clicks; }));
    }
};

} // namespace

TEST_CASE("container-bound floating content stays inside its container", "[floating][container]") {
    detail::Scope scope;
    detail::Scope::Use use(scope);
    ContainerScene scene;
    Window window(scene.view(true));
    window.layout();
    // No room beside the anchor in the container (190..270): kept inside it, at 120..200.
    window.click({125, 110});
    CHECK(scene.content_clicks == 1);
    CHECK(scene.shown.peek());
    window.click({230, 110}); // where it would have gone: the sibling's, and outside it
    CHECK_FALSE(scene.shown.peek());
    CHECK(scene.sibling_clicks == 0); // the press only closed it
    window.click({230, 110});
    CHECK(scene.sibling_clicks == 1);
}

TEST_CASE("without a container, container-bound content uses the window", "[floating][container]") {
    detail::Scope scope;
    detail::Scope::Use use(scope);
    ContainerScene scene;
    Window window(scene.view(false));
    window.layout();
    window.click({230, 110}); // beside the anchor, over the sibling
    CHECK(scene.content_clicks == 1);
    CHECK(scene.sibling_clicks == 0);
}

TEST_CASE("presses and Esc close container-bound content from anywhere in the window", "[floating][container]") {
    detail::Scope scope;
    detail::Scope::Use use(scope);
    ContainerScene scene;
    Window window(scene.view(true));
    window.layout();
    window.click({20, 250}); // in the container, outside the content
    CHECK_FALSE(scene.shown.peek());
    CHECK(scene.container_clicks == 0);

    scene.shown = true;
    window.layout();
    window.events->key({.kind = KeyEvent::Kind::down, .key = Key::escape});
    window.layout();
    CHECK_FALSE(scene.shown.peek());
}

TEST_CASE("container-bound content is drawn clipped to its container", "[floating][container]") {
    detail::Scope scope;
    detail::Scope::Use use(scope);
    ContainerScene scene;
    Window window(scene.view(true));
    window.layout();
    detail::DisplayList list;
    window.root->paint(list);
    bool clipped = false;
    for (const auto& command : list.commands) {
        if (const auto* clip = std::get_if<detail::PushClipCommand>(&command)) {
            clipped |= clip->rect.x == 0 && clip->rect.width == 200 && clip->rect.height == 300;
        }
    }
    CHECK(clipped);
}

TEST_CASE("container-bound content moves with its container", "[floating][container]") {
    detail::Scope scope;
    detail::Scope::Use use(scope);
    ContainerScene scene;
    Window window(scene.view(true).offset(0, 50));
    window.layout();
    window.click({125, 160}); // 50 lower: 120..200 x 150..190
    CHECK(scene.content_clicks == 1);
    window.click({125, 110}); // where it was: in the container, outside it
    CHECK_FALSE(scene.shown.peek());
}
