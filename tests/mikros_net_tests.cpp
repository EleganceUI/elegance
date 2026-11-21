// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Net as the Mikros build has it: the stand-in for downloads (no Boost, no OpenSSL) in place of the real
// ones, linked with Core only. Starting a download fails with PlatformError, and nothing is cached
// (ELGSPEC-0040.R21).

#include "net/fetch.h"
#include "net/http_cache.h"

#include <elg/error.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <filesystem>
#include <string>

using namespace elg;
using Catch::Matchers::ContainsSubstring;

TEST_CASE("on Mikros, starting a download fails, and nothing is called back", "[mikros]") {
    bool called = false;
    std::string message;
    try {
        detail::net::fetch("https://example.com/a.png", [&](detail::net::FetchResult) { called = true; });
    } catch (const PlatformError& error) {
        message = error.what();
    }
    CHECK_THAT(message, ContainsSubstring("Mikros doesn't offer networking"));
    CHECK_FALSE(called);
}

TEST_CASE("on Mikros, there is no folder for the downloads' cache", "[mikros]") {
    CHECK_FALSE(detail::net::HttpCache::default_directory().has_value());
    detail::net::HttpCache cache(std::filesystem::path("cache-that-is-not-made"), 1024);
    CHECK_NOTHROW(cache.clear());
    CHECK_FALSE(std::filesystem::exists("cache-that-is-not-made"));
}
