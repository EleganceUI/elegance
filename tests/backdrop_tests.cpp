// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// backdrop_blur: what's behind the view, blurred and adjusted, under everything it wraps.

#include "render/frame_plan.h"
#include "view/view_node.h"

#include <elg/shapes.h>
#include <elg/views.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <optional>
#include <variant>
#include <vector>

using namespace elg;

namespace {

detail::DisplayList paint(detail::Element& root) {
    root.size_that_fits({100.f, 50.f});
    root.place({}, {100.f, 50.f});
    detail::DisplayList list;
    root.paint(list);
    return list;
}

// The backdrops drawn, in order.
std::vector<detail::BackdropFill> backdrops(const detail::DisplayList& list) {
    std::vector<detail::BackdropFill> found;
    for (const detail::DrawCommand& command : list.commands) {
        const ShapeStyle* fill = nullptr;
        if (const auto* rect = std::get_if<detail::RectCommand>(&command)) {
            fill = &rect->fill;
        } else if (const auto* path = std::get_if<detail::PathCommand>(&command)) {
            fill = &path->fill;
        }
        if (const auto* backdrop = fill ? std::get_if<detail::BackdropFill>(&fill->value()) : nullptr) {
            found.push_back(*backdrop);
        }
    }
    return found;
}

} // namespace

TEST_CASE("a backdrop blur goes behind what it wraps", "[backdrop]") {
    detail::Scope scope;
    detail::Scope::Use use(scope);
    const auto root = detail::lower(Rectangle()
                                        .fill(Color::white)
                                        .background(Color::black.opacity(0.5f))
                                        .backdrop_blur(20, {.saturation = 1.8f, .brightness = -0.1f, .noise = 0.02f}),
                                    {});
    const detail::DisplayList list = paint(*root);
    const std::vector<detail::BackdropFill> found = backdrops(list);
    REQUIRE(found.size() == 1);
    CHECK(found[0].blur == 20);
    CHECK(found[0].saturation == 1.8f);
    CHECK(found[0].brightness == -0.1f);
    CHECK(found[0].noise == 0.02f);
    // First: the backdrop, then the tint, then the content.
    const auto* first = std::get_if<detail::RectCommand>(&list.commands.front());
    REQUIRE(first);
    CHECK(std::holds_alternative<detail::BackdropFill>(first->fill.value()));
}

TEST_CASE("a backdrop blur follows its state", "[backdrop]") {
    detail::Scope scope;
    detail::Scope::Use use(scope);
    State<float> radius(10.f);
    State<float> saturation(1.f);
    const auto root =
        detail::lower(Rectangle().fill(Color::clear).backdrop_blur(radius, Capsule(), {.saturation = saturation}), {});
    REQUIRE(backdrops(paint(*root)).front().blur == 10);
    radius = 30.f;
    saturation = 0.f;
    detail::flush_effects();
    const detail::BackdropFill now = backdrops(paint(*root)).front();
    CHECK(now.blur == 30);
    CHECK(now.saturation == 0);
}

TEST_CASE("the renderer blurs the backdrop and adjusts its colors", "[backdrop]") {
    detail::Scope scope;
    detail::Scope::Use use(scope);
    const auto plain = detail::lower(Rectangle().fill(Color::clear).backdrop_blur(8), {});
    const detail::FramePlan without = detail::plan_frame(paint(*plain), 100, 50);
    REQUIRE(without.backdrops.size() == 1);
    CHECK(without.backdrops[0].sigma == 4); // half the radius, at scale 1
    CHECK(without.main.reads_backdrop);

    const auto adjusted = detail::lower(Rectangle().fill(Color::clear).backdrop_blur(8, {.brightness = 0.2f}), {});
    const detail::FramePlan with = detail::plan_frame(paint(*adjusted), 100, 50);
    CHECK(without.stops.size() < 6);   // no color matrix (just the buffer's placeholder)
    REQUIRE(with.stops.size() == 6);   // one color matrix, six entries
    const std::array<float, 4>& offsets = with.stops[with.stops.size() - 2];
    CHECK(offsets[0] == 0.2f); // brightness is added to red, green and blue
    CHECK(offsets[2] == 0.2f);
    CHECK(offsets[3] == 0);
}
