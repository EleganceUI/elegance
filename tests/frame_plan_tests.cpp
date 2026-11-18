// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// What the frame plan draws of clipped content (ELGSPEC-0024.R4).

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
