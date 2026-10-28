// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Accessibility modifiers (design.md §12). The tree itself is checked through Windows UI
// Automation against the running sandbox (design.md, M5).

#include <elg/elegance.h>

#include <catch2/catch_test_macros.hpp>

using namespace elg;

TEST_CASE("accessibility modifiers compose", "[accessibility]") {
    const State<int> count(3);
    CHECK_NOTHROW(Button(Image("trash"), [] {}).accessibility_label("Delete draft").accessibility_hint("Can't be undone"));
    CHECK_NOTHROW(Text("50%").accessibility_value([=] { return std::to_string(count()) + " of 10"; }));
    CHECK_NOTHROW(Image("sparkle").accessibility_hidden());
    CHECK_NOTHROW(Text("Settings").accessibility_role(AccessibilityRole::heading));
    CHECK_NOTHROW(HStack(Image("user"), Text("Ada"), Text("Online")).accessibility_element());
    CHECK_NOTHROW(Circle().on_click([] {}).accessibility_action([] {}).accessibility_label("Record"));
}

TEST_CASE("accessibility actions need a function", "[accessibility]") {
    CHECK_THROWS_AS(Text("x").accessibility_action({}), UsageError);
}
