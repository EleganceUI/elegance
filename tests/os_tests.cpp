// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// What the library asks the operating system through the backend interfaces (core/os_backend.h,
// net/net_backend.h, text/text_backend.h).

#include "core/crash.h"
#include "core/dynamic_library.h"
#include "core/os_backend.h"
#include "net/http_cache.h"
#include "net/net_backend.h"
#include "platform/platform.h"
#include "text/text_backend.h"

#include <elg/app.h>
#include <elg/error.h>
#include <elg/events.h>
#include <elg/platform.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_exception.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <map>
#include <optional>
#include <string>

using namespace elg;

namespace {

namespace fs = std::filesystem;

// Reads environment variables from `variables` instead of the process's environment.
auto environment(std::map<std::string, std::string> variables) {
    return [variables = std::move(variables)](const char* name) -> std::optional<std::string> {
        const auto it = variables.find(name);
        return it == variables.end() ? std::nullopt : std::optional(it->second);
    };
}

} // namespace

TEST_CASE("a stack trace lists the caller's frames", "[os]") {
    const std::string text = detail::to_text(detail::StackTrace::current());
    CHECK(text != "(unavailable)");
    CHECK(std::ranges::count(text, '\n') >= 2); // this test's frame and the ones that called it
}

TEST_CASE("an environment variable that isn't set has no value", "[os]") {
    CHECK_FALSE(detail::os::environment_variable("ELG_TESTS_SURELY_NOT_SET").has_value());
}

TEST_CASE("a shared library that can't be loaded throws with its path", "[os]") {
    CHECK_THROWS_MATCHES(detail::DynamicLibrary("elg-tests-no-such-library"), ResourceError,
                         Catch::Matchers::MessageMatches(Catch::Matchers::StartsWith(
                             "can't load \"elg-tests-no-such-library\"")));
}

TEST_CASE("a shell command's output can be read", "[os]") {
    CHECK(detail::os::process_id() > 0);
    std::FILE* pipe = detail::os::open_command_pipe("echo elg");
    REQUIRE(pipe != nullptr);
    std::string output;
    char buffer[256];
    while (std::fgets(buffer, sizeof(buffer), pipe)) {
        output += buffer;
    }
    CHECK(detail::os::close_command_pipe(pipe) == 0);
    CHECK(output.starts_with("elg"));
}

TEST_CASE("the image cache is in the user's cache folder, by the program's name", "[os]") {
    CHECK(detail::os::executable_path().stem() == "elegance_tests");
    if (const std::optional<fs::path> folder = detail::net::HttpCache::default_directory()) {
        CHECK(folder->parent_path().filename() == "elegance_tests");
    }

    using detail::net::image_cache_folder;
    const std::map<std::string, std::string> every{
        {"HOME", "/home/me"}, {"XDG_CACHE_HOME", "/xdg"}, {"LOCALAPPDATA", "C:/Local"}};
    switch (current_platform()) {
    case Platform::windows:
        CHECK(image_cache_folder("app", environment(every)) == fs::path("C:/Local") / "Elegance" / "app" / "Images");
        break;
    case Platform::macos:
        CHECK(image_cache_folder("app", environment(every)) ==
              fs::path("/home/me") / "Library" / "Caches" / "Elegance" / "app" / "Images");
        break;
    case Platform::linux:
        CHECK(image_cache_folder("app", environment(every)) == fs::path("/xdg") / "elegance" / "app" / "images");
        // An empty XDG_CACHE_HOME counts as unset.
        CHECK(image_cache_folder("app", environment({{"HOME", "/home/me"}, {"XDG_CACHE_HOME", ""}})) ==
              fs::path("/home/me") / ".cache" / "elegance" / "app" / "images");
        break;
    }
    CHECK_FALSE(image_cache_folder("app", environment({})).has_value());
}

TEST_CASE("the glyph cache is in the user's cache folder", "[os]") {
    using detail::glyph_cache_folder;
    const std::map<std::string, std::string> every{
        {"HOME", "/home/me"}, {"XDG_CACHE_HOME", "/xdg"}, {"LOCALAPPDATA", "C:/Local"}};
    switch (current_platform()) {
    case Platform::windows:
        CHECK(glyph_cache_folder(environment(every)) == fs::path("C:/Local") / "Elegance" / "GlyphCache");
        break;
    case Platform::macos:
        CHECK(glyph_cache_folder(environment(every)) ==
              fs::path("/home/me") / "Library" / "Caches" / "Elegance" / "GlyphCache");
        break;
    case Platform::linux:
        CHECK(glyph_cache_folder(environment(every)) == fs::path("/xdg") / "elegance" / "glyphs");
        CHECK(glyph_cache_folder(environment({{"HOME", "/home/me"}})) ==
              fs::path("/home/me") / ".cache" / "elegance" / "glyphs");
        break;
    }
    CHECK_FALSE(glyph_cache_folder(environment({})).has_value());
}

TEST_CASE("the system's root certificates are its store's, or its distribution's bundle", "[os]") {
    const detail::net::SystemRootCertificates roots = detail::net::system_root_certificates();
    if (current_platform() == Platform::linux) {
        CHECK(roots.certificates.empty());
        CHECK(std::ranges::find(roots.bundles, fs::path("/etc/ssl/certs/ca-certificates.crt")) != roots.bundles.end());
    } else {
        CHECK_FALSE(roots.certificates.empty());
        CHECK(roots.bundles.empty());
    }
}

TEST_CASE("threads end before Elegance's static objects only in a Windows DLL", "[os]") {
    if (current_platform() != Platform::windows) {
        CHECK_FALSE(detail::os::threads_end_before_static_destruction());
    }
}

TEST_CASE("the platform names its text editing modifiers and caption glyphs", "[os]") {
    if (current_platform() == Platform::macos) {
        CHECK(detail::platform::word_modifier() == KeyModifiers::alt);
    } else {
        CHECK(detail::platform::word_modifier() == KeyModifiers::control);
    }
    if (current_platform() != Platform::windows) {
        CHECK_FALSE(detail::platform::caption_icon_font().has_value());
    }
}

TEST_CASE("the platform and its shortcut modifier are those of the OS it was built for", "[os]") {
#if defined(_WIN32)
    CHECK(current_platform() == Platform::windows);
    CHECK(primary_modifier() == KeyModifiers::control);
#elif defined(__APPLE__)
    CHECK(current_platform() == Platform::macos);
    CHECK(primary_modifier() == KeyModifiers::super);
#else
    CHECK(current_platform() == Platform::linux);
    CHECK(primary_modifier() == KeyModifiers::control);
#endif
    CHECK(KeyboardShortcut{Key::s}.modifiers == primary_modifier());
}
