// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Read-only arguments are Props: a plain value, or state the view follows.

#include "view/view_node.h"

#include <elg/shapes.h>
#include <elg/views.h>

#include <catch2/catch_test_macros.hpp>

using namespace elg;

namespace {
bool same(Size a, Size b) {
    return a.width == b.width && a.height == b.height;
}
} // namespace

TEST_CASE("padding follows the state it's bound to", "[prop]") {
    detail::Scope scope;
    detail::Scope::Use use(scope);
    State<float> inset(10.f);
    auto root = detail::lower(Rectangle().frame(20, 20).padding(inset), {});
    CHECK(same(root->size_that_fits({}), {40, 40}));

    inset = 4.f;
    detail::flush_effects();
    CHECK(same(root->size_that_fits({}), {28, 28}));

    State<float> top(0.f);
    auto edges = detail::lower(Rectangle().frame(20, 20).padding(Edges::top, top), {});
    CHECK(same(edges->size_that_fits({}), {20, 20}));
    top = 6.f;
    detail::flush_effects();
    CHECK(same(edges->size_that_fits({}), {20, 26}));
}

TEST_CASE("plain padding values still take braces", "[prop]") {
    detail::Scope scope;
    detail::Scope::Use use(scope);
    auto root = detail::lower(Rectangle().frame(20, 20).padding({.top = 1, .leading = 2, .bottom = 3, .trailing = 4}), {});
    CHECK(same(root->size_that_fits({}), {26, 24}));
}

TEST_CASE("a slider's range follows state", "[prop]") {
    detail::Scope scope;
    detail::Scope::Use use(scope);
    State<float> value(5.f);
    State<float> max(10.f);
    auto root = detail::lower(Slider(value.binding(), {.min = 0, .max = max}), {});
    detail::AccessibilityInfo info;
    root->accessibility(info);
    CHECK(info.maximum == 10.0);

    max = 50.f;
    detail::flush_effects();
    info = {};
    root->accessibility(info);
    CHECK(info.maximum == 50.0);
    CHECK(value() == 5.f); // a new range doesn't write the value
}
