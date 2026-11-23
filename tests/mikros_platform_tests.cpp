// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// The Mikros platform's hooks for notifications, the dock and tray icons (platform/mikros/native.cpp),
// called directly: notify() and TrayIcon reach them only inside a running App (ELGSPEC-0040.R21). This is
// the one test that includes the SDL platform's header, with a maintainer's permission.

#include "gpu/fence_backend.h"
#include "gpu/pipeline_cache_backend.h"
#include "platform/sdl/native.h"

#include <elg/desktop.h>
#include <elg/error.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <functional>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

using namespace elg;
using Catch::Matchers::ContainsSubstring;

namespace {

// What `value` holds. The test fails when it holds nothing.
template <typename T>
T required(std::optional<T> value) {
    if (!value) {
        throw std::runtime_error("a value that the test needs is missing");
    }
    return std::move(*value);
}

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

TEST_CASE("on Mikros, the pipeline cache is in the app's cache folder, and the shipped one in its package", "[mikros]") {
    const auto folders_for = [](const std::map<std::string, std::string>& environment) {
        return elg::detail::gpu::pipeline_cache_folders("app", [&](const char* name) -> std::optional<std::string> {
            const auto found = environment.find(name);
            return found == environment.end() ? std::nullopt : std::optional(found->second);
        });
    };
    const auto installed = folders_for({{"HOME", "/apps/app"}, {"XDG_CACHE_HOME", "/apps/app/cache"}});
    CHECK(required(installed.own).generic_string() == "/apps/app/cache/elegance/pipelines");
    CHECK(required(installed.shipped).generic_string() == "/apps/app/package/pipelines");
    // An app that has no cache folder gets no cache of its own, and HOME, its data folder, isn't one.
    const auto portable = folders_for({{"HOME", "/home/folder"}});
    CHECK_FALSE(portable.own.has_value());
    CHECK_FALSE(folders_for({{"HOME", "/apps/app"}, {"XDG_CACHE_HOME", "relative"}}).own.has_value());
    CHECK_FALSE(folders_for({{"XDG_CACHE_HOME", "/apps/app/cache"}}).shipped.has_value());
    CHECK_FALSE(folders_for({}).own.has_value());
}

TEST_CASE("on Mikros, the Vulkan fences that a producer shares are timelines", "[mikros]") {
    CHECK(elg::detail::gpu::vulkan_fences_are_timelines());
}
