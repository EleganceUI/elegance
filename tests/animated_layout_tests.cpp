// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// While an animation runs, each frame lays out again only what it moves (ELGSPEC-0029.R12).

#include "animation/frame_clock.h"
#include "view/element.h"
#include "view/view_node.h"

#include <elg/elegance.h>

#include <catch2/catch_test_macros.hpp>

#include <span>
#include <vector>

using namespace elg;

namespace {

// Lays out its one child at its top-left corner, counting how often it does.
class Counting final : public Layout {
public:
    explicit Counting(int& arranged) : arranged_(&arranged) {}

    [[nodiscard]] Size size_that_fits(ProposedSize proposal, std::span<const LayoutChild> children) const override {
        return children[0].size_that_fits(proposal);
    }
    void place_children(Rect bounds, ProposedSize proposal, std::span<const LayoutChild> children) const override {
        ++*arranged_;
        children[0].place({bounds.x, bounds.y}, proposal);
    }

private:
    int* arranged_;
};

// A frame of a 200 x 200 window: effects, then layout, with the animation of the change if any.
void lay_out(detail::Element& root, const Animation* animation = nullptr) {
    detail::flush_effects();
    const detail::LayoutAnimationScope animated(animation);
    root.size_that_fits({200.f, 200.f});
    root.place({}, {200.f, 200.f});
}

} // namespace

TEST_CASE("while an animation runs, only what it moves is laid out again", "[layout][animation]") {
    detail::Scope scope;
    const detail::Scope::Use use(scope);
    detail::set_frame_time(100);
    const State<float> width(20);
    int around = 0; // around what the animation resizes
    int beside = 0; // next to it
    const auto root = detail::lower(
        VStack({.alignment = HorizontalAlignment::leading, .spacing = 0},
               Counting(around)(Rectangle().frame([width] { return Size{width(), 20}; }, Alignment::top_leading)),
               Counting(beside)(Rectangle().frame(20, 20))),
        {});
    lay_out(*root);
    REQUIRE(around == 1);
    REQUIRE(beside == 1);

    // The rectangle grows over a second: the layout around it lays out again for its new size,
    // the one beside it, which stays where it is, doesn't.
    width = 60;
    const Animation animation = Animation::linear(1);
    lay_out(*root, &animation);
    CHECK(around == 2);
    CHECK(beside == 1);

    // As it grows, the frames ease there by themselves: nothing is laid out again.
    const auto rectangle_width = [&] {
        std::vector<detail::Element*> path;
        REQUIRE(root->hit_test({5, 5}, path));
        return path.back()->frame().width;
    };
    for (const double t : {100.25, 100.5, 100.75, 101.0, 101.5}) {
        detail::set_frame_time(t);
        lay_out(*root);
        if (t == 100.5) {
            CHECK(rectangle_width() == 40); // halfway
        }
    }
    CHECK(rectangle_width() == 60);
    CHECK(around == 2);
    CHECK(beside == 1);
    detail::set_frame_time(0); // later tests start from 0
}
