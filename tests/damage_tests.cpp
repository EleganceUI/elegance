// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Which part of a window a frame draws again: the damage between two frames' display lists
// (ELGSPEC-0029.R4, R5), and the drawn area it's merged into (ELGSPEC-0029.R3).

#include "display/display_list.h"
#include "render/damage.h"
#include "render/frame_mailbox.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <utility>
#include <vector>

using namespace elg;

namespace {

detail::RectCommand rect(Rect r, Color color = Color::black) {
    return {.rect = r, .fill = ShapeStyle(color)};
}

std::vector<Rect> damage(const detail::DisplayList& before, const detail::DisplayList& now, std::uint32_t side = 200) {
    return detail::damage_between(detail::footprints(before, side, side), detail::footprints(now, side, side));
}

// The bounding box of a damage region; empty when there's none.
Rect box(const std::vector<Rect>& rects) {
    if (rects.empty()) {
        return {};
    }
    Rect out = rects.front();
    for (const Rect& r : rects) {
        out = detail::union_of(out, r);
    }
    return out;
}

detail::DisplayList list_of(std::vector<detail::DrawCommand> commands, float scale = 1) {
    detail::DisplayList list;
    list.scale = scale;
    list.commands = std::move(commands);
    return list;
}

} // namespace

TEST_CASE("a frame that draws the same has no damage", "[damage]") {
    const auto list = list_of({rect({0, 0, 200, 200}, Color::white), rect({10, 10, 20, 20})});
    CHECK(damage(list, list).empty());
}

TEST_CASE("a changed command damages its bounds, a pixel around them for anti-aliasing", "[damage]") {
    const auto before = list_of({rect({0, 0, 200, 200}, Color::white), rect({10, 10, 20, 20})});
    SECTION("its color") {
        const auto now = list_of({rect({0, 0, 200, 200}, Color::white), rect({10, 10, 20, 20}, Color::rgb(255, 0, 0))});
        CHECK(box(damage(before, now)) == Rect{9, 9, 22, 22});
    }
    SECTION("its place: where it was and where it is") {
        const auto now = list_of({rect({0, 0, 200, 200}, Color::white), rect({100, 10, 20, 20})});
        const std::vector<Rect> rects = damage(before, now);
        REQUIRE(rects.size() == 2);
        CHECK(box(rects) == Rect{9, 9, 112, 22});
    }
    SECTION("a caret that blinks off: only where it was") {
        const auto now = list_of({rect({0, 0, 200, 200}, Color::white)});
        CHECK(damage(before, now) == std::vector<Rect>{{9, 9, 22, 22}});
    }
    SECTION("at a fractional place: rounded out to whole pixels") {
        const auto now = list_of({rect({0, 0, 200, 200}, Color::white), rect({10.5f, 10.25f, 20, 20})});
        CHECK(box(damage(before, now)) == Rect{9, 9, 23, 23});
    }
    SECTION("at a scale of 2") {
        CHECK(box(damage(list_of(before.commands, 2), list_of({rect({0, 0, 200, 200}, Color::white)}, 2), 400)) ==
              Rect{19, 19, 42, 42});
    }
}

TEST_CASE("a command added before others damages only itself", "[damage]") {
    const auto before = list_of({rect({0, 0, 10, 10}), rect({50, 50, 10, 10}), rect({100, 100, 10, 10})});
    const auto now = list_of({rect({150, 0, 10, 10}), rect({0, 0, 10, 10}), rect({50, 50, 10, 10}),
                              rect({100, 100, 10, 10})});
    CHECK(damage(before, now) == std::vector<Rect>{{149, 0, 12, 11}});
}

TEST_CASE("a path counts by its points, not by the set that holds them", "[damage]") {
    const auto path = [](std::vector<Point> points) {
        const auto polygons = std::make_shared<const detail::Polygons>(detail::Polygons{std::move(points)});
        return detail::PathCommand{polygons, {0, 0, 20, 20}, 1, {10, 10}, {10, 10, 20, 20}, ShapeStyle(Color::black)};
    };
    const auto before = list_of({path({{0, 0}, {20, 0}, {0, 20}})});
    CHECK(damage(before, list_of({path({{0, 0}, {20, 0}, {0, 20}})})).empty());
    CHECK(box(damage(before, list_of({path({{0, 0}, {20, 0}, {20, 20}})}))) == Rect{9, 9, 22, 22});
}

TEST_CASE("commands that swap places in painter's order count as changed", "[damage]") {
    const auto a = rect({0, 0, 50, 50}, Color::rgb(255, 0, 0));
    const auto b = rect({25, 25, 50, 50}, Color::rgb(0, 0, 255));
    CHECK_FALSE(damage(list_of({a, b}), list_of({b, a})).empty());
}

TEST_CASE("a command's bounds are those of its layers and clips", "[damage]") {
    SECTION("cut by its clip") {
        const auto before = list_of({detail::PushClipCommand{.rect = {0, 0, 50, 50}}, rect({40, 40, 100, 100}),
                                     detail::PopClipCommand{}});
        const auto now = list_of({detail::PushClipCommand{.rect = {0, 0, 50, 50}},
                                   rect({40, 40, 100, 100}, Color::rgb(255, 0, 0)), detail::PopClipCommand{}});
        CHECK(box(damage(before, now)) == Rect{39, 39, 11, 11});
    }
    SECTION("widened by its layer's blur") {
        const detail::PushLayerCommand blurred{.bounds = {0, 0, 200, 200}, .blur = 10};
        const auto before = list_of({blurred, rect({90, 90, 20, 20}), detail::PopLayerCommand{}});
        const auto now = list_of({blurred, rect({90, 90, 20, 20}, Color::rgb(255, 0, 0)), detail::PopLayerCommand{}});
        const Rect damaged = box(damage(before, now));
        CHECK(damaged.x < 89 - 20); // 4 sigma (20 px) and more
        CHECK(damaged.max_x() > 111 + 20);
    }
    SECTION("moved by its layer's transform") {
        const detail::PushLayerCommand moved{.bounds = {0, 0, 50, 50},
                                             .transform = detail::LayerTransform{1, 0, 0, 1, {0, 0}}};
        detail::PushLayerCommand doubled = moved;
        doubled.transform = detail::LayerTransform{2, 0, 0, 2, {0, 0}};
        const auto before = list_of({doubled, rect({10, 10, 10, 10}), detail::PopLayerCommand{}});
        const auto now = list_of({doubled, rect({10, 10, 10, 10}, Color::rgb(255, 0, 0)), detail::PopLayerCommand{}});
        CHECK(box(damage(before, now)) == Rect{18, 18, 24, 24});
    }
}

TEST_CASE("a layer's opacity damages the layer, not more", "[damage]") {
    const auto content = rect({20, 20, 30, 30});
    const auto before = list_of({rect({150, 150, 10, 10}), detail::PushLayerCommand{.bounds = {20, 20, 30, 30}},
                                 content, detail::PopLayerCommand{}});
    const auto now = list_of({rect({150, 150, 10, 10}),
                              detail::PushLayerCommand{.bounds = {20, 20, 30, 30}, .opacity = 0.5f}, content,
                              detail::PopLayerCommand{}});
    CHECK(box(damage(before, now)) == Rect{19, 19, 32, 32});
}

TEST_CASE("a command moved into a layer counts as changed", "[damage]") {
    const auto content = rect({20, 20, 30, 30});
    const auto layer = detail::PushLayerCommand{.bounds = {20, 20, 30, 30}, .opacity = 0.5f};
    const auto before = list_of({layer, detail::PopLayerCommand{}, content});
    const auto now = list_of({layer, content, detail::PopLayerCommand{}});
    CHECK(box(damage(before, now)) == Rect{19, 19, 32, 32});
}

TEST_CASE("damage under a backdrop blur damages all that it draws", "[damage]") {
    const auto glass = detail::RectCommand{.rect = {0, 0, 100, 100}, .fill = ShapeStyle(BackdropFill{.blur = 10})};
    const auto before = list_of({rect({0, 0, 200, 200}, Color::white), rect({150, 150, 10, 10}), glass});
    SECTION("damage it reads") {
        // A change behind the glass, within its blur's reach.
        const auto now = list_of({rect({0, 0, 200, 200}, Color::white), rect({150, 150, 10, 10}),
                                  rect({110, 50, 5, 5}), glass});
        CHECK(box(damage(before, now)) == Rect{-0.f, 0, 116, 101});
    }
    SECTION("damage it doesn't read") {
        const auto now = list_of({rect({0, 0, 200, 200}, Color::white), rect({150, 150, 10, 10}, Color::rgb(255, 0, 0)),
                                  glass});
        CHECK(box(damage(before, now)) == Rect{149, 149, 12, 12});
    }
}

TEST_CASE("a scroll's content is damaged where it shows, in the window", "[damage]") {
    // A 100 x 100 viewport at (50, 50), its content scrolled 40 points: a row at y 60 shows at 70.
    const auto scroll = [](float scrolled) {
        return detail::PushScrollCommand{.id = 1, .viewport = {50, 50, 100, 100}, .origin = {50, 50 - scrolled}};
    };
    const auto before = list_of({scroll(40), rect({0, 60, 100, 20}), rect({0, 400, 100, 20}), detail::PopScrollCommand{}});
    SECTION("what changed in it") {
        const auto now = list_of({scroll(40), rect({0, 60, 100, 20}, Color::rgb(255, 0, 0)), rect({0, 400, 100, 20}),
                                  detail::PopScrollCommand{}});
        CHECK(box(damage(before, now)) == Rect{50, 69, 100, 22});
    }
    SECTION("scrolled: all that shows of it") {
        const auto now = list_of({scroll(50), rect({0, 60, 100, 20}), rect({0, 400, 100, 20}), detail::PopScrollCommand{}});
        CHECK(box(damage(before, now)) == Rect{50, 50, 100, 100});
    }
}

TEST_CASE("a scroll's content moves with it, and scaled, is put in window points", "[damage]") {
    std::vector<detail::DrawCommand> commands{
        detail::PushScrollCommand{.id = 1, .viewport = {50, 50, 100, 100}, .origin = {50, 10}},
        rect({0, 60, 100, 20}), detail::PopScrollCommand{}, rect({0, 0, 10, 10})};
    SECTION("moved") {
        detail::transform(commands, 5, 7, 1, {});
        const auto& scroll = std::get<detail::PushScrollCommand>(commands[0]);
        CHECK(scroll.viewport == Rect{55, 57, 100, 100});
        CHECK(scroll.origin == Point{55, 17});
        CHECK(std::get<detail::RectCommand>(commands[1]).rect == Rect{0, 60, 100, 20}); // in its own points
        CHECK(std::get<detail::RectCommand>(commands[3]).rect == Rect{5, 7, 10, 10});
        CHECK(detail::draw_bounds(commands) == Rect{5, 7, 150, 150});
    }
    SECTION("scaled") {
        detail::transform(commands, 0, 0, 2, {});
        CHECK(std::get<detail::PushClipCommand>(commands[0]).rect == Rect{100, 100, 200, 200});
        CHECK(std::get<detail::RectCommand>(commands[1]).rect == Rect{100, 140, 200, 40});
        CHECK(std::holds_alternative<detail::PopClipCommand>(commands[2]));
    }
}

TEST_CASE("the drawn area merges the damage into at most 4 rectangles", "[damage]") {
    SECTION("rects whose bounding box is at most twice their area are merged") {
        CHECK(detail::drawn_area({{0, 0, 10, 10}, {10, 0, 10, 10}}) == std::vector<Rect>{{0, 0, 20, 10}});
    }
    SECTION("rects far apart stay apart") {
        CHECK(detail::drawn_area({{0, 0, 10, 10}, {180, 180, 10, 10}}).size() == 2);
    }
    SECTION("past 4, the cheapest merges") {
        std::vector<Rect> spread;
        spread.reserve(10);
        for (int i = 0; i < 10; ++i) {
            spread.push_back({static_cast<float>(i * 30), static_cast<float>((i % 3) * 60), 5, 5});
        }
        const std::vector<Rect> drawn = detail::drawn_area(spread);
        CHECK(drawn.size() <= 4);
        for (const Rect& r : spread) {
            CHECK(std::ranges::any_of(drawn, [&](const Rect& d) {
                return r.x >= d.x && r.y >= d.y && r.max_x() <= d.max_x() && r.max_y() <= d.max_y();
            }));
        }
    }
    SECTION("nothing to draw") {
        CHECK(detail::drawn_area({}).empty());
    }
}

TEST_CASE("a frame that replaces another in the mailbox keeps the request to show the window again", "[damage]") {
    detail::FrameMailbox mailbox;
    detail::WindowFrame exposed;
    exposed.window_id = 1;
    exposed.show_again = true;
    detail::WindowFrame later;
    later.window_id = 1;
    detail::WindowFrame other;
    other.window_id = 2;
    mailbox.post(std::move(exposed));
    mailbox.post(std::move(later));
    mailbox.post(std::move(other));
    detail::RenderBatch batch;
    REQUIRE(mailbox.take(batch));
    REQUIRE(batch.frames.size() == 2);
    for (const detail::WindowFrame& frame : batch.frames) {
        CHECK(frame.show_again == (frame.window_id == 1));
    }
}
