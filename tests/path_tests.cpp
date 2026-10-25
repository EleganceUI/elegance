// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Path geometry: building, SVG path data, fill rules, flattening, and stroking.

#include <elg/elegance.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace elg;
using Catch::Approx;

TEST_CASE("rects and circles", "[path]") {
    Path rect;
    rect.add_rect({10, 20, 30, 40});
    CHECK(rect.contains({25, 40}));
    CHECK_FALSE(rect.contains({5, 40}));
    const Rect bounds = rect.bounds();
    CHECK(bounds.x == Approx(10));
    CHECK(bounds.max_y() == Approx(60));

    Path circle;
    circle.add_circle({0, 0}, 10);
    CHECK(circle.contains({0, 0}));
    CHECK(circle.contains({9, 0}));
    CHECK_FALSE(circle.contains({8, 8})); // outside the circle, inside its box
    CHECK(circle.bounds().width == Approx(20).margin(0.1));
}

TEST_CASE("fill rules decide overlaps", "[path]") {
    // Two nested squares wound the same way: non-zero fills the middle, even-odd makes a hole.
    Path rings;
    rings.add_rect({0, 0, 100, 100}).add_rect({25, 25, 50, 50});
    CHECK(rings.contains({50, 50}, FillRule::non_zero));
    CHECK_FALSE(rings.contains({50, 50}, FillRule::even_odd));
    CHECK(rings.contains({10, 10}, FillRule::even_odd));
}

TEST_CASE("SVG path data", "[path]") {
    const Path triangle = Path::svg("M10 10 L20 10 L20 20 Z");
    CHECK(triangle.contains({18, 12}));
    CHECK_FALSE(triangle.contains({12, 18}));

    // Relative commands, implicit lines after a move, H/V.
    CHECK(Path::svg("m10 10 10 0 v10 h-10 z") == Path::svg("M10 10 L20 10 L20 20 L10 20 Z"));

    // A half circle of radius 5 above the x axis, with packed arc flags.
    const Rect arc = Path::svg("M0 0A5 5 0 0110 0").bounds();
    CHECK(arc.width == Approx(10).margin(0.05));
    CHECK(arc.height == Approx(5).margin(0.05));

    // Smooth curves and exponents parse.
    CHECK_NOTHROW(Path::svg("M0,0 C10,0 10,10 0,10 S-10,20 0,20 Q5,25 10,20 T20,20 l1e1-2.5e0"));

    CHECK_THROWS_AS(Path::svg("M0 0 L"), UsageError);
    CHECK_THROWS_AS(Path::svg("10 10"), UsageError);
    CHECK_THROWS_AS(Path::svg("M0 0 X5 5"), UsageError);
}

TEST_CASE("flattening stays within tolerance", "[path]") {
    Path circle;
    circle.add_circle({0, 0}, 100);
    for (const auto& line : circle.flatten(0.25f)) {
        REQUIRE(line.closed);
        for (const Point p : line.points) {
            CHECK(std::sqrt(p.x * p.x + p.y * p.y) == Approx(100).margin(0.3));
        }
    }
}

TEST_CASE("strokes: width, caps, joins, dashes", "[path]") {
    Path line;
    line.move_to({0, 0}).line_to({10, 0});

    const Path butt = line.stroked({.width = 2});
    CHECK(butt.contains({5, 0.9f}));
    CHECK_FALSE(butt.contains({5, 1.1f}));
    CHECK_FALSE(butt.contains({-0.5f, 0}));
    CHECK(line.stroked({.width = 2, .cap = LineCap::square}).contains({-0.9f, 0.9f}));
    const Path round = line.stroked({.width = 2, .cap = LineCap::round});
    CHECK(round.contains({-0.9f, 0}));
    CHECK_FALSE(round.contains({-0.9f, 0.9f}));

    // A right-angle corner: the miter fills the outer corner, a bevel cuts it off.
    Path corner;
    corner.move_to({0, 0}).line_to({10, 0}).line_to({10, 10});
    CHECK(corner.stroked({.width = 2, .join = LineJoin::miter}).contains({10.9f, -0.9f}));
    CHECK_FALSE(corner.stroked({.width = 2, .join = LineJoin::bevel}).contains({10.9f, -0.9f}));
    // Past the miter limit, miters bevel.
    CHECK_FALSE(corner.stroked({.width = 2, .join = LineJoin::miter, .miter_limit = 1}).contains({10.9f, -0.9f}));

    const Path dashes = line.stroked({.width = 2, .dash = {2, 2}});
    CHECK(dashes.contains({1, 0}));
    CHECK_FALSE(dashes.contains({3, 0}));
    CHECK(dashes.contains({5, 0}));
    const Path shifted = line.stroked({.width = 2, .dash = {2, 2}, .dash_phase = 2});
    CHECK_FALSE(shifted.contains({1, 0}));
    CHECK(shifted.contains({3, 0}));
}

TEST_CASE("transforms scale then move", "[path]") {
    Path square;
    square.add_rect({0, 0, 1, 1});
    const Rect r = square.transformed(24, 24, 100, 50).bounds();
    CHECK(r.x == Approx(100));
    CHECK(r.y == Approx(50));
    CHECK(r.width == Approx(24));
}
