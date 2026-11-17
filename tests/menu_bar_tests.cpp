// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// The menu bar's items as resolved for showing, and how a chosen one is found again.

#include "platform/menu_bar.h"

#include <elg/app.h>

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <vector>

using namespace elg;
namespace menus = elg::detail::menus;

namespace {

menus::Source menus_of(std::vector<MenuBarItem> menus) {
    return std::make_shared<const std::vector<MenuBarItem>>(std::move(menus));
}

} // namespace

TEST_CASE("a resolved item finds its item in the menus it came from, not in later ones", "[menus]") {
    const menus::Source first = menus_of({{.title = "File", .submenu = {{.title = "Open", .action = [] {}}}}});
    const std::vector<menus::Item> shown = menus::resolve(first, false);
    REQUIRE(shown.size() == 1);
    REQUIRE(shown[0].submenu.size() == 1);
    const menus::Item& open = shown[0].submenu[0];
    CHECK(open.source == first);

    // The app gives other menus: the same position is another item.
    const menus::Source second = menus_of({{.title = "Edit", .submenu = {{.title = "Undo", .action = [] {}}}}});
    const std::vector<menus::Item> later = menus::resolve(second, false);
    REQUIRE(later[0].submenu[0].id == open.id);

    const MenuBarItem* found = menus::find(*open.source, open.id);
    REQUIRE(found);
    CHECK(found->title == "Open");
}
