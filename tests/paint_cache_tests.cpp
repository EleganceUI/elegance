// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Elements whose drawing didn't change add the commands they built before, moved with them,
// instead of building them again (ELGSPEC-0029.R8). Each frame here is also painted afresh, by
// every element, and must be the same.

#include "animation/frame_clock.h"
#include "display/display_list.h"
#include "render/damage.h"
#include "text/text_system.h"
#include "view/element.h"
#include "view/view_node.h"

#include <elg/elegance.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <memory>

using namespace elg;

namespace {

// Whether the frame's own paint runs, not the one afresh that it's compared with: canvases count
// their draws in it.
bool g_counting = false;

detail::TextSystem& text_system() {
    static detail::TextSystem system;
    return system;
}

// Whether two frames draw the same: the same commands, by the damage between them.
bool same(const detail::DisplayList& a, const detail::DisplayList& b) {
    return a.commands.size() == b.commands.size() &&
           detail::damage_between(detail::footprints(a, 1000, 1000), detail::footprints(b, 1000, 1000)).empty();
}

// A 200 x 200 window of `view`, laid out and painted as the app does each frame.
struct Window {
    explicit Window(const View& view, float pixels = 1) : scale(pixels) {
        root = detail::lower(view, {.text = &text_system(), .needs_frame = &needs_frame});
    }

    // Effects, layout, then paint; and the same frame painted afresh, to compare.
    void frame() const {
        detail::flush_effects();
        {
            const detail::LayoutScaleScope pixels(scale);
            root->size_that_fits({200.f, 200.f});
            root->place({}, {200.f, 200.f});
        }
        detail::DisplayList list;
        list.scale = scale;
        {
            const detail::PaintScope paint(epoch);
            g_counting = true;
            root->paint(list);
            g_counting = false;
        }
        detail::DisplayList fresh;
        fresh.scale = scale;
        {
            const detail::PaintScope paint(epoch, true);
            root->paint(fresh);
        }
        CHECK(same(list, fresh));
    }

    float scale;
    std::uint64_t epoch = 0;
    bool needs_frame = false;
    std::unique_ptr<detail::Element> root;
};

// A 40 x 30 canvas that counts its draws.
View counted(int& draws, Color color = Color::black) {
    return Canvas([&draws, color](GraphicsContext& graphics) {
               draws += g_counting ? 1 : 0;
               graphics.fill_rect({0, 0, 40, 30}, color);
           })
        .frame(40, 30);
}

} // namespace

TEST_CASE("an element whose drawing didn't change isn't painted again", "[paint]") {
    detail::Scope scope;
    const detail::Scope::Use use(scope);
    const State<int> state(0);
    int draws = 0;
    Window window(VStack({.spacing = 0}, counted(draws),
                         Rectangle()
                             .fill([state] { return ShapeStyle(state() == 0 ? Color::rgb(0, 0, 255) : Color::rgb(255, 0, 0)); })
                             .frame(40, 30),
                         Text([state] { return std::to_string(state()); })));
    window.frame();
    CHECK(draws == 1);
    window.frame();
    CHECK(draws == 1); // nothing changed

    // The rectangle's fill and the text change; the canvas, above them, stays where it was.
    state = 1;
    window.frame();
    CHECK(draws == 1);

    // Drawn again from scratch when the window's drawing starts over, or its scale changes.
    ++window.epoch;
    window.frame();
    CHECK(draws == 2);
    window.scale = 2;
    window.frame();
    CHECK(draws == 3);
}

TEST_CASE("an element that moved by whole pixels adds what it built, moved", "[paint]") {
    detail::Scope scope;
    const detail::Scope::Use use(scope);
    const State<float> above(10);
    int draws = 0;
    // At 1.5 pixels a point: 2 points are 3 pixels, 1 point is 1.5.
    const Window window(VStack({.spacing = 0}, Rectangle().fill(Color::black).frame(40, 10).padding(Edges::top, [above] {
                             return above();
                         }),
                         counted(draws)),
                  1.5f);
    window.frame();
    REQUIRE(draws == 1);

    above = 12; // moved down 3 pixels
    window.frame();
    CHECK(draws == 1);
    above = 13; // half a pixel off: its drawing on whole pixels is another
    window.frame();
    CHECK(draws == 2);
}

TEST_CASE("an element moved by an offset adds what it built, under the offset", "[paint]") {
    detail::Scope scope;
    const detail::Scope::Use use(scope);
    const State<float> x(0);
    int draws = 0;
    const Window window(counted(draws).offset([x] { return x(); }, 0.f));
    window.frame();
    x = 25;
    window.frame();
    x = 30.5f;
    window.frame();
    CHECK(draws == 1); // what the offset moves is drawn as it was
}

TEST_CASE("drawing that changes over time is built in every frame", "[paint]") {
    detail::Scope scope;
    const detail::Scope::Use use(scope);
    int draws = 0;
    const Window window(Canvas([&draws](GraphicsContext& graphics) {
                      draws += g_counting ? 1 : 0;
                      graphics.fill_rect({0, 0, static_cast<float>(graphics.time()), 10}, Color::black);
                  }).frame(40, 30));
    detail::set_frame_time(10);
    window.frame();
    detail::set_frame_time(20);
    window.frame();
    CHECK(draws == 2);
    detail::set_frame_time(0); // later tests start from 0
}

TEST_CASE("an animation's frames draw it where it is, then it's kept once it ends", "[paint]") {
    detail::Scope scope;
    const detail::Scope::Use use(scope);
    detail::set_frame_time(100);
    const State<bool> on(false);
    int draws = 0;
    const Window window(VStack({.spacing = 0}, counted(draws),
                         Rectangle()
                             .fill([on] { return ShapeStyle(on() ? Color::rgb(255, 0, 0) : Color::rgb(0, 0, 255)); })
                             .frame(40, 30)
                             .opacity([on] { return on() ? 1.f : 0.5f; })));
    window.frame();
    with_animation(Animation::linear(1), [&] { on = true; });
    for (const double t : {100.0, 100.25, 100.5, 101.0, 102.0}) {
        detail::set_frame_time(t);
        window.frame(); // each the same as painted afresh
    }
    CHECK(draws == 1);
    detail::set_frame_time(0);
}

TEST_CASE("scrolled content moves, without being painted again", "[paint]") {
    detail::Scope scope;
    const detail::Scope::Use use(scope);
    std::array<int, 10> draws{};
    const Window window(ScrollView(VStack({.spacing = 0}, counted(draws[0]), counted(draws[1]), counted(draws[2]),
                                    counted(draws[3]), counted(draws[4]), counted(draws[5]), counted(draws[6]),
                                    counted(draws[7]), counted(draws[8]), counted(draws[9]))));
    window.frame();
    // Canvases may draw outside their frames, so none is left out.
    CHECK(draws == std::array<int, 10>{1, 1, 1, 1, 1, 1, 1, 1, 1, 1});

    EventHandler* scroll = window.root->event_handler();
    REQUIRE(scroll);
    scroll->scroll(ScrollEvent{.dy = 30, .precise = true}, window.root->frame());
    window.frame();
    CHECK(draws == std::array<int, 10>{1, 1, 1, 1, 1, 1, 1, 1, 1, 1});
}
