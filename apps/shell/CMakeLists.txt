# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, You can obtain one at
# https://mozilla.org/MPL/2.0/.

add_executable(shell
        apps.cpp
        chrome.cpp
        main.cpp
        model.cpp
)

target_link_libraries(shell PRIVATE Elegance::Elegance)

if (MSVC)
    target_compile_options(shell PRIVATE /W4 /permissive- /utf-8)
else ()
    target_compile_options(shell PRIVATE -Wall -Wextra -Wpedantic)
endif ()
