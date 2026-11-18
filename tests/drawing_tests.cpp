// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// What drawing puts on screen, checked by its pixels (ELGSPEC-0024). GPU tests: they skip where
// the GPU draws nothing (Dawn's Null backend).

#include "render_pixels.h"

#include "display/display_list.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>

using namespace elg;
using elg_tests::PixelRenderer;
using elg_tests::Pixels;

namespace {

std::shared_ptr<const detail::Polygons> rect_polygons(float width, float height) {
    return std::make_shared<const detail::Polygons>(
        detail::Polygons{{{0, 0}, {width, 0}, {width, height}, {0, height}}});
}

} // namespace

TEST_CASE("content 20000 pixels tall draws its last part when scrolled to its end", "[.gpu][large-content]") {
    PixelRenderer renderer;
    if (!renderer.draws()) {
        SKIP("the GPU here draws nothing");
    }
    detail::DisplayList list;
    list.clear_color = Color::white;

    SECTION("a path") {
        // Its last 50 pixels show, at the top of the window.
        list.commands.emplace_back(detail::PathCommand{rect_polygons(100, 20000), {0, 0, 100, 20000}, 1, {0, -19950},
                                                       {}, ShapeStyle(Color::black), FillRule::non_zero});
        const Pixels pixels = renderer.draw(list, 100, 100);
        CHECK(pixels.at(50, 10) == std::array{0, 0, 0, 255});
        CHECK(pixels.at(50, 45) == std::array{0, 0, 0, 255});
        CHECK(pixels.at(50, 60) == std::array{255, 255, 255, 255});
    }
    SECTION("a layer") {
        // Its content: a red rect at its end, which shows at the top of the window.
        list.commands.emplace_back(detail::PushLayerCommand{.bounds = {0, -19950, 100, 20000}});
        list.commands.emplace_back(
            detail::RectCommand{.rect = {0, -19950, 100, 20000}, .fill = ShapeStyle(Color::black)});
        list.commands.emplace_back(
            detail::RectCommand{.rect = {0, 0, 100, 50}, .fill = ShapeStyle(Color::rgb(255, 0, 0))});
        list.commands.emplace_back(detail::PopLayerCommand{});
        const Pixels pixels = renderer.draw(list, 100, 100);
        const std::array<int, 4> red = pixels.at(50, 25);
        CHECK(red[0] >= 250);
        CHECK(red[1] <= 5);
        CHECK(red[2] <= 5);
        CHECK(pixels.at(50, 75) == std::array{255, 255, 255, 255});
    }
}
