// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// The Mikros platform's hooks for notifications, the dock and tray icons (platform/mikros/native.cpp),
// called directly: notify() and TrayIcon reach them only inside a running App (ELGSPEC-0040.R21). This is
// the one test that includes the SDL platform's header, with a maintainer's permission.

#include "platform/sdl/native.h"

#include <elg/desktop.h>
#include <elg/error.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <optional>
#include <string>

using namespace elg;
using Catch::Matchers::ContainsSubstring;

namespace {

template <typename Action>
std::string platform_error_of(const Action& action) {
    try {
        action();
    } catch (const PlatformError& error) {
        return error.what();
    }
    return {};
}

} // namespace

TEST_CASE("on Mikros, a notification and a tray icon fail with PlatformError", "[mikros]") {
    namespace native = detail::platform::native;
    CHECK_THAT(platform_error_of([] { native::show_notification({.title = "a", .body = "b"}); }),
               ContainsSubstring("Mikros doesn't offer notifications"));
    CHECK_THAT(platform_error_of([] { native::require_tray_icons(); }),
               ContainsSubstring("Mikros doesn't offer tray icons"));
}

TEST_CASE("on Mikros, the dock's badge and progress do nothing, and so does forgetting notifications", "[mikros]") {
    namespace native = detail::platform::native;
    CHECK_NOTHROW(native::set_badge(std::string("3")));
    CHECK_NOTHROW(native::set_badge(std::nullopt));
    CHECK_NOTHROW(native::set_progress(0.5, ProgressState::normal));
    CHECK_NOTHROW(native::set_progress(std::nullopt, ProgressState::indeterminate));
    CHECK_NOTHROW(native::forget_notification_handlers());
}
