// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include <elg/grid.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace elg;
using Catch::Approx;

namespace {

std::vector<GridTrack> tracks(std::initializer_list<GridItem> items, std::optional<float> length) {
    const std::vector<GridItem> list(items);
    return resolve_grid_tracks(list, length);
}

} // namespace

TEST_CASE("fixed tracks take their size", "[grid]") {
    const auto t = tracks({GridItem::fixed(40), GridItem::fixed(60)}, 500.f);
    REQUIRE(t.size() == 2);
    CHECK(t[0].size == Approx(40));
    CHECK(t[1].size == Approx(60));
    CHECK(grid_tracks_length(t) == Approx(40 + default_stack_spacing + 60));
}

TEST_CASE("flexible tracks share what fixed ones leave", "[grid]") {
    const auto t = tracks({GridItem::fixed(100).with_spacing(10), GridItem::flexible().with_spacing(10), GridItem::flexible()},
                          420.f);
    REQUIRE(t.size() == 3);
    CHECK(t[1].size == Approx(150)); // (420 - 100 - 20) / 2
    CHECK(t[2].size == Approx(150));
    CHECK(grid_tracks_length(t) == Approx(420));
}

TEST_CASE("flexible tracks respect their limits", "[grid]") {
    const auto t = tracks({GridItem::flexible(10, 50).with_spacing(0), GridItem::flexible().with_spacing(0)}, 300.f);
    CHECK(t[0].size == Approx(50));  // capped
    CHECK(t[1].size == Approx(250)); // takes the rest
    const auto narrow = tracks({GridItem::flexible(120).with_spacing(0), GridItem::flexible().with_spacing(0)}, 200.f);
    CHECK(narrow[0].size == Approx(120)); // its minimum
    CHECK(narrow[1].size == Approx(80));
}

TEST_CASE("an adaptive item makes as many tracks as fit", "[grid]") {
    const auto t = tracks({GridItem::adaptive(150).with_spacing(10)}, 500.f);
    REQUIRE(t.size() == 3); // 3 * 150 + 2 * 10 = 470 fits, 4 don't
    CHECK(t[0].size == Approx((500 - 20) / 3.f));
    CHECK(grid_tracks_length(t) == Approx(500));

    const auto too_narrow = tracks({GridItem::adaptive(150)}, 100.f);
    REQUIRE(too_narrow.size() == 1); // always at least one
    CHECK(too_narrow[0].size == Approx(100));

    const auto capped = tracks({GridItem::adaptive(100, 120).with_spacing(0)}, 500.f);
    REQUIRE(capped.size() == 5);
    CHECK(capped[0].size == Approx(100));
}

TEST_CASE("without a length tracks take their ideal sizes", "[grid]") {
    const auto t = tracks({GridItem::fixed(30), GridItem::flexible(40), GridItem::adaptive(80)}, std::nullopt);
    REQUIRE(t.size() == 3);
    CHECK(t[0].size == Approx(30));
    CHECK(t[1].size == Approx(40));
    CHECK(t[2].size == Approx(80));
}

TEST_CASE("the grid's spacing is the default between tracks", "[grid]") {
    const std::vector<GridItem> items{GridItem::adaptive(100), GridItem::fixed(50).with_spacing(4), GridItem::fixed(50)};
    const auto t = resolve_grid_tracks(items, 400.f, 20.f);
    CHECK(t[0].spacing == Approx(20)); // adaptive tracks: the grid's
    CHECK(t[t.size() - 2].spacing == Approx(4)); // its own
}

TEST_CASE("no items is one flexible track", "[grid]") {
    const auto t = resolve_grid_tracks({}, 300.f);
    REQUIRE(t.size() == 1);
    CHECK(t[0].size == Approx(300));
}

TEST_CASE("track alignment carries through", "[grid]") {
    const auto t = tracks({GridItem::fixed(50).with_alignment(Alignment::leading), GridItem::adaptive(40)}, 200.f);
    CHECK(t[0].alignment == Alignment::leading);
    CHECK_FALSE(t.back().alignment.has_value());
}
