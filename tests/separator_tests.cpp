// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Separator: a line along either axis, read as a separator.

#include "view/accessibility.h"
#include "view/view_node.h"

#include <elg/views.h>

#include <algorithm>

#include <catch2/catch_test_macros.hpp>

using namespace elg;

namespace {

Size size_of(const View& view, ProposedSize proposal) {
    return detail::lower(view, {})->size_that_fits(proposal);
}

} // namespace

TEST_CASE("a separator spans its axis at its thickness", "[separator]") {
    detail::Scope scope;
    detail::Scope::Use use(scope);
    const Size horizontal = size_of(Separator(), {200.f, 100.f});
    CHECK(horizontal.width == 200);
    CHECK(horizontal.height == 1);

    const Size vertical = size_of(Separator(Axis::vertical, {.thickness = 2, .inset = 10}), {200.f, 100.f});
    CHECK(vertical.width == 2);
    CHECK(vertical.height == 100);
}

TEST_CASE("a separator reads as one", "[separator]") {
    detail::Scope scope;
    detail::Scope::Use use(scope);
    const auto root = detail::lower(VStack(Separator(), Separator().accessibility_hidden()), {});
    root->size_that_fits({200.f, 100.f});
    root->place({}, {200.f, 100.f});
    const detail::AccessTree tree = detail::build_accessibility_tree(*root, "Window", {200, 100}, nullptr);
    const auto separators = std::ranges::count_if(
        tree.nodes, [](const detail::AccessNode& node) { return node.info.role == detail::AccessRole::separator; });
    CHECK(separators == 1); // the hidden one isn't there
    for (const detail::AccessNode& node : tree.nodes) {
        CHECK_FALSE(node.info.can_focus);
    }
}
