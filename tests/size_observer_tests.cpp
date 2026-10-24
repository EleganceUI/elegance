// This Source Code Form is subject to the terms of the Mozilla Public License,
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at https://mozilla.org/MPL/2.0/.

#include "view/view_node.h"

#include <catch2/catch_test_macros.hpp>
#include <elg/shapes.h>

#include <limits>

using namespace elg;

TEST_CASE("size observers report mount, resize and DPI without relayout",
                    "[size-observer]") {
    detail::Scope scope;
    detail::Scope::Use use(scope);
    bool needs_frame = false;
    std::vector<PixelSize> sizes;
    const View view = Rectangle().on_size_change(
            [&](PixelSize size) { sizes.push_back(size); });
    auto root = detail::lower(view, {.needs_frame = &needs_frame});

    root->notify_render_size(2);
    CHECK(sizes.empty()); // No speculative measurements before placement.
    root->size_that_fits({500, 500});
    root->size_that_fits({10, 10});
    CHECK(sizes.empty());
    root->place({}, {100, 50});
    needs_frame = false; // Mounting reactive effects requests the initial frame.
    CHECK(sizes.empty()); // Delivered only after the full layout pass.
    root->notify_render_size(2);
    REQUIRE(sizes.size() == 1);
    CHECK(sizes.back() == PixelSize{200, 100});
    CHECK_FALSE(needs_frame); // Observation alone doesn't request another frame.

    root->place({}, {100, 50}); // Cached layout.
    root->notify_render_size(2);
    CHECK(sizes.size() == 1);
    root->notify_render_size(1.5f); // Same logical frame; a different display.
    REQUIRE(sizes.size() == 2);
    CHECK(sizes.back() == PixelSize{150, 75});

    root->place({}, {100.25f, 50.25f});
    root->notify_render_size(1.5f);
    REQUIRE(sizes.size() == 3);
    CHECK(sizes.back() == PixelSize{151, 76});
    root->place({}, {100.3f, 50.3f}); // Same physical dimensions after rounding.
    root->notify_render_size(1.5f);
    CHECK(sizes.size() == 3);
    root->place({20, 30}, {100.3f, 50.3f}); // Position is not size.
    root->notify_render_size(1.5f);
    CHECK(sizes.size() == 3);

    auto second = detail::lower(view, {}); // Same description, independent mount.
    second->place({}, {100.3f, 50.3f});
    second->notify_render_size(1.5f);
    REQUIRE(sizes.size() == 4);
    CHECK(sizes.back() == PixelSize{151, 76});
    root.reset();
    auto remounted = detail::lower(view, {});
    remounted->place({}, {100.3f, 50.3f});
    remounted->notify_render_size(1.5f);
    CHECK(sizes.size() == 5);
}

TEST_CASE("size observation sees nested layout bounds and zero sizes without "
                    "painting",
                    "[size-observer]") {
    detail::Scope scope;
    detail::Scope::Use use(scope);
    std::vector<PixelSize> sizes;
    const View view =
            Rectangle()
                    .on_size_change([&](PixelSize size) { sizes.push_back(size); })
                    .padding(10);
    auto root = detail::lower(view, {});
    root->place({}, {100, 80});
    root->notify_render_size(2);
    REQUIRE(sizes.size() == 1);
    CHECK(sizes.back() == PixelSize{160, 120});
    root->place({}, {20, 20});
    root->notify_render_size(2);
    REQUIRE(sizes.size() == 2);
    CHECK(sizes.back() == PixelSize{0, 0});
    root->notify_render_size(
            3); // A DPI change that doesn't change physical dimensions.
    CHECK(sizes.size() == 2);
}

TEST_CASE(
        "size observation ignores invalid scale and saturates oversized dimensions",
        "[size-observer]") {
    detail::Scope scope;
    detail::Scope::Use use(scope);
    std::vector<PixelSize> sizes;
    auto root = detail::lower(Rectangle().on_size_change(
                                                                [&](PixelSize size) { sizes.push_back(size); }),
                                                        {});
    root->place({}, {100, 50});
    root->notify_render_size(0);
    root->notify_render_size(std::numeric_limits<float>::quiet_NaN());
    CHECK(sizes.empty());
    root->notify_render_size(std::numeric_limits<float>::max());
    REQUIRE(sizes.size() == 1);
    CHECK(sizes.back() == PixelSize{UINT32_MAX, UINT32_MAX});
}
