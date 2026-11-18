// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// What the frame plan draws of clipped content (ELGSPEC-0024.R4), and the offscreen images it
// plans for large content: what shows of it, and what its effects reach (ELGSPEC-0024.R5, R6).

#include "display/display_list.h"
#include "render/frame_plan.h"

#include <catch2/catch_test_macros.hpp>

#include <limits>
#include <memory>
#include <optional>

using namespace elg;

namespace {

// A layer of a 100 x 100 black rect, masked by `mask`.
detail::FramePlan masked_layer(const detail::PathCommand& mask) {
    detail::DisplayList list;
    list.commands.emplace_back(detail::PushLayerCommand{.bounds = {0, 0, 100, 100}, .mask = mask});
    list.commands.emplace_back(detail::RectCommand{.rect = {0, 0, 100, 100}, .fill = ShapeStyle(Color::black)});
    list.commands.emplace_back(detail::PopLayerCommand{});
    return detail::plan_frame(list, 100, 100);
}

// Whether the layer's mask keeps none of it: its last segment is a clear, through no path.
bool keeps_nothing(const detail::FramePlan& plan) {
    if (plan.layers.size() != 1 || plan.layers[0].segments.empty()) {
        return false;
    }
    const detail::PlanSegment& last = plan.layers[0].segments.back();
    const detail::gpu::QuadInstance& clear = plan.instances[last.first];
    return last.mask && last.path < 0 && last.layer < 0 && last.count == 1 && clear.color[3] == 0;
}

detail::PathCommand circle_mask(std::shared_ptr<const detail::Polygons> polygons, float scale, Rect bounds) {
    return {std::move(polygons), bounds, scale, {50, 50}, {}, ShapeStyle(Color::black), FillRule::non_zero};
}

} // namespace

TEST_CASE("content clipped to a shape that covers no pixel draws nothing", "[render][clip]") {
    const auto square =
        std::make_shared<const detail::Polygons>(detail::Polygons{{{-10, -10}, {10, -10}, {10, 10}, {-10, 10}}});
    const Rect square_bounds{-10, -10, 20, 20};

    // The mask at its size: it keeps what it covers, through its coverage.
    const detail::FramePlan shown = masked_layer(circle_mask(square, 1, square_bounds));
    REQUIRE(shown.layers.size() == 1);
    CHECK(shown.layers[0].segments.back().path >= 0);
    CHECK_FALSE(keeps_nothing(shown));

    SECTION("a shape scaled to zero") {
        CHECK(keeps_nothing(masked_layer(circle_mask(square, 0, square_bounds))));
    }
    SECTION("a path that flattened to nothing") {
        CHECK(keeps_nothing(masked_layer(circle_mask(std::make_shared<const detail::Polygons>(), 0, {}))));
        CHECK(keeps_nothing(masked_layer(circle_mask(nullptr, 1, {}))));
    }
    SECTION("a path with no area") {
        const auto line = std::make_shared<const detail::Polygons>(detail::Polygons{{{-10, 0}, {10, 0}}});
        CHECK(keeps_nothing(masked_layer(circle_mask(line, 1, {-10, 0, 20, 0}))));
    }
    SECTION("a shape whose size isn't finite") {
        constexpr float nan = std::numeric_limits<float>::quiet_NaN();
        CHECK(keeps_nothing(masked_layer(circle_mask(square, nan, square_bounds))));
    }
}

namespace {

std::shared_ptr<const detail::Polygons> rect_polygons(float width, float height) {
    return std::make_shared<const detail::Polygons>(
        detail::Polygons{{{0, 0}, {width, 0}, {width, height}, {0, height}}});
}

// A 100 x 100 black rect at `at`, in a layer.
detail::DisplayList layer_at(Point at, detail::PushLayerCommand layer = {}) {
    layer.bounds = {at.x, at.y, 100, 100};
    detail::DisplayList list;
    list.commands.emplace_back(layer);
    list.commands.emplace_back(detail::RectCommand{.rect = layer.bounds, .fill = ShapeStyle(Color::black)});
    list.commands.emplace_back(detail::PopLayerCommand{});
    return list;
}

} // namespace

TEST_CASE("a path's mask holds what shows of it, wherever it is", "[render][large-content]") {
    // 20,000 pixels tall, scrolled to its last 50: past 8192 pixels from its top.
    detail::DisplayList list;
    list.commands.emplace_back(detail::PathCommand{rect_polygons(100, 20000), {0, 0, 100, 20000}, 1, {0, -19950}, {},
                                                   ShapeStyle(Color::black), FillRule::non_zero});
    const detail::FramePlan plan = detail::plan_frame(list, 100, 100);
    REQUIRE(plan.paths.size() == 1);
    CHECK(plan.paths[0].width == 100);
    CHECK(plan.paths[0].height == 51); // its last 50 pixels, and one for anti-aliasing
    CHECK(plan.paths[0].offset[1] == -19950);

    // Scrolled on, the mask holds another part of it: it isn't the same mask.
    detail::DisplayList further;
    further.commands.emplace_back(detail::PathCommand{rect_polygons(100, 20000), {0, 0, 100, 20000}, 1, {0, -19900},
                                                      {}, ShapeStyle(Color::black), FillRule::non_zero});
    const detail::FramePlan next = detail::plan_frame(further, 100, 100);
    REQUIRE(next.paths.size() == 1);
    CHECK(next.paths[0].key != plan.paths[0].key);

    SECTION("a path outside its window gets no mask") {
        detail::DisplayList outside;
        outside.commands.emplace_back(detail::PathCommand{rect_polygons(100, 100), {0, 0, 100, 100}, 1, {0, 300}, {},
                                                          ShapeStyle(Color::black), FillRule::non_zero});
        CHECK(detail::plan_frame(outside, 100, 100).paths.empty());
    }
}

TEST_CASE("a layer's image covers what shows of it, and what its effects reach", "[render][large-content]") {
    SECTION("a layer much larger than its window") {
        detail::DisplayList list;
        list.commands.emplace_back(detail::PushLayerCommand{.bounds = {-5000, -5000, 20000, 20000}, .opacity = 0.5f});
        list.commands.emplace_back(
            detail::RectCommand{.rect = {-5000, -5000, 20000, 20000}, .fill = ShapeStyle(Color::black)});
        list.commands.emplace_back(detail::PopLayerCommand{});
        const detail::FramePlan plan = detail::plan_frame(list, 100, 100);
        REQUIRE(plan.layers.size() == 1);
        CHECK(plan.layers[0].width == 100);
        CHECK(plan.layers[0].height == 100);
    }
    SECTION("with a blur, as far around as it reaches") {
        detail::DisplayList list;
        list.commands.emplace_back(detail::PushLayerCommand{.bounds = {-5000, 0, 20000, 100}, .blur = 10});
        list.commands.emplace_back(
            detail::RectCommand{.rect = {-5000, 0, 20000, 100}, .fill = ShapeStyle(Color::black)});
        list.commands.emplace_back(detail::PopLayerCommand{});
        const detail::FramePlan plan = detail::plan_frame(list, 100, 100);
        REQUIRE(plan.layers.size() == 1);
        // sigma 5: 3 sigma and 2 on each side, and a texel for the downsampling.
        CHECK(plan.layers[0].width == 100 + 2 * 18);
    }
    SECTION("a layer outside its window gets no image") {
        CHECK(detail::plan_frame(layer_at({0, 500}), 100, 100).layers.empty());
    }
    SECTION("nor one its clip hides") {
        detail::DisplayList list;
        list.commands.emplace_back(detail::PushClipCommand{.rect = {0, 0, 100, 10}});
        for (const detail::DrawCommand& command : layer_at({0, 50}).commands) {
            list.commands.push_back(command);
        }
        list.commands.emplace_back(detail::PopClipCommand{});
        list.commands.emplace_back(detail::RectCommand{.rect = {0, 0, 10, 10}, .fill = ShapeStyle(Color::black)});
        const detail::FramePlan plan = detail::plan_frame(list, 100, 100);
        CHECK(plan.layers.empty());
        CHECK_FALSE(plan.main.segments.empty()); // what follows the layer is still drawn
    }
    SECTION("nor one scaled to zero") {
        CHECK(detail::plan_frame(layer_at({0, 0}, {.transform = detail::LayerTransform{0, 0, 0, 0, {50, 50}}}), 100,
                                 100)
                  .layers.empty());
    }
    SECTION("a shadow that shows brings the content it's made from") {
        // The layer lies past the window's right edge; its shadow, 150 points to its left, doesn't.
        const Shadow shadow{.color = Color::black, .radius = 4, .x = -150, .y = 0};
        const detail::FramePlan plan = detail::plan_frame(layer_at({150, 0}, {.shadow = shadow}), 100, 100);
        REQUIRE(plan.layers.size() == 1);
        CHECK(plan.layers[0].width >= 100);
    }
}
