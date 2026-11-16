// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// What the library asks the operating system through the backend interfaces (core/os_backend.h).

#include "core/crash.h"
#include "core/dynamic_library.h"
#include "core/os_backend.h"

#include <elg/error.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_exception.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <string>

using namespace elg;

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
