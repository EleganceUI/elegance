// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Children that come and go while the app runs.

#include "render/display_list.h"
#include "view/view_node.h"

#include <elg/reactive.h>
#include <elg/shapes.h>
#include <elg/views.h>

#include <catch2/catch_test_macros.hpp>

#include <vector>

using namespace elg;

namespace {

// The innermost element at `point`, or null.
detail::Element* hit(detail::Element& root, Point point) {
    std::vector<detail::Element*> path;
    return root.hit_test(point, path) && !path.empty() ? path.back() : nullptr;
}

void layout(detail::Element& root) {
    detail::flush_effects();
    root.size_that_fits({20, 100});
    root.place({}, {20, 100});
}

} // namespace

TEST_CASE("a lazy stack gives each item with a repeated key its own row", "[lazy]") {
    detail::Scope scope;
    detail::Scope::Use use(scope);
    const State<std::vector<int>> items(std::vector<int>{1, 1, 2});
    int built = 0;
    auto root = detail::lower(LazyVStack({.spacing = 0}, ForEach(items, [](int item) { return item; },
                                                                [&](int) {
                                                                    ++built;
                                                                    return Rectangle().frame(20, 20);
                                                                })),
                              {});
    layout(*root);
    CHECK(built == 3);
    detail::Element* const first = hit(*root, {10, 5});
    detail::Element* const second = hit(*root, {10, 25});
    REQUIRE(first);
    REQUIRE(second);
    CHECK(first != second);
    detail::DisplayList list;
    root->paint(list); // the rows drawn are the ones that exist

    // Laid out again with the same items: nothing is rebuilt.
    layout(*root);
    CHECK(built == 3);
    CHECK(hit(*root, {10, 25}) == second);

    // The items change: the repeated one is rebuilt, the others are kept.
    items = std::vector<int>{1, 1, 2, 3};
    layout(*root);
    CHECK(built == 5);
    CHECK(hit(*root, {10, 5}) == first);
}
