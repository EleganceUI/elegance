// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// When a Canvas calls its draw function, with what, and when it asks to be drawn again.

#include "display/display_list.h"
#include "view/view_node.h"

#include <elg/elegance.h>

#include <catch2/catch_test_macros.hpp>

#include <memory>

using namespace elg;

namespace {

// A canvas in a 40 x 30 frame of a 100 x 100 window, with what its draw calls saw.
struct Drawn {
    int draws = 0;
    Size size;
    float scale = 0;
};

void lay_out(detail::Element& root) {
    root.size_that_fits({100.f, 100.f});
    root.place({}, {100.f, 100.f});
}

void paint(detail::Element& root) {
    detail::DisplayList list;
    list.scale = 2;
    root.paint(list);
}

} // namespace

TEST_CASE("a canvas draws only when painted, with its laid-out frame and the window's scale", "[canvas]") {
    detail::Scope scope;
    const detail::Scope::Use use(scope);
    Drawn drawn;
    bool needs_frame = false;
    const auto root = detail::lower(Canvas([&](GraphicsContext& graphics) {
                                        ++drawn.draws;
                                        drawn.size = graphics.size();
                                        drawn.scale = graphics.scale();
                                    }).frame(40, 30),
                                    {.needs_frame = &needs_frame});
    CHECK(drawn.draws == 0); // not when built
    lay_out(*root);
    CHECK(drawn.draws == 0); // nor when laid out
    paint(*root);
    CHECK(drawn.draws == 1);
    CHECK(drawn.size.width == 40);
    CHECK(drawn.size.height == 30);
    CHECK(drawn.scale == 2);
}

TEST_CASE("a canvas asks for a frame when what it drew with changes, and draws once in it", "[canvas]") {
    detail::Scope scope;
    const detail::Scope::Use use(scope);
    const State<int> count{0};
    const State<int> unread{0};
    int draws = 0;
    int seen = -1;
    bool needs_frame = false;
    const auto root = detail::lower(Canvas([&](GraphicsContext&) {
                                        ++draws;
                                        seen = count();
                                    }).frame(40, 30),
                                    {.needs_frame = &needs_frame});
    lay_out(*root);
    paint(*root);
    REQUIRE(draws == 1);
    needs_frame = false;

    unread = 1; // not read while drawing
    detail::flush_effects();
    CHECK_FALSE(needs_frame);

    count = 1;
    count = 2;
    detail::flush_effects();
    CHECK(needs_frame);
    CHECK(draws == 1); // the change only asks for a frame
    paint(*root);
    CHECK(draws == 2);
    CHECK(seen == 2);
}
