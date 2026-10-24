// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Desktop integration (design.md §13.4). The dialogs, notifications, and tray need a running App
// (and a person to click), so these check the rules around them.

#include <elg/elegance.h>

#include <catch2/catch_test_macros.hpp>

using namespace elg;

TEST_CASE("desktop services need a running app", "[desktop]") {
    CHECK_THROWS_AS(choose_files({}, [](std::vector<std::filesystem::path>) {}), UsageError);
    CHECK_THROWS_AS(choose_save_file({}, [](std::optional<std::filesystem::path>) {}), UsageError);
    CHECK_THROWS_AS(choose_folders({}, [](std::vector<std::filesystem::path>) {}), UsageError);
    CHECK_THROWS_AS(notify({.title = "x"}), UsageError);
    CHECK_THROWS_AS(TrayIcon({.tooltip = "x"}), UsageError);
}

TEST_CASE("drop targets need a handler", "[desktop]") {
    CHECK_THROWS_AS(Text("x").on_drop({}), UsageError);
    CHECK_NOTHROW(Text("x").on_drop({.files = [](const std::vector<std::filesystem::path>&, Point) { return true; }}));
    CHECK_NOTHROW(Text("x").on_drop({.text = [](const std::string&, Point) { return true; }, .targeted = [](bool) {}}));
}
