# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, You can obtain one at
# https://mozilla.org/MPL/2.0/.

find_package(Catch2 3 CONFIG REQUIRED)
include(Catch)

add_executable(elegence_tests
        reactive_tests.cpp
)
target_link_libraries(elegence_tests PRIVATE Elegence::Elegence Catch2::Catch2WithMain)

if (MSVC)
    target_compile_options(elegence_tests PRIVATE /W4 /permissive- /utf-8)
else ()
    target_compile_options(elegence_tests PRIVATE -Wall -Wextra -Wpedantic)
endif ()

catch_discover_tests(elegence_tests)
