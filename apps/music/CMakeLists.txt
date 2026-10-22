# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, You can obtain one at
# https://mozilla.org/MPL/2.0/.

add_executable(music
        library.cpp
        main.cpp
        model.cpp
        pages.cpp
        ui.cpp
)

target_link_libraries(music PRIVATE Elegence::Elegence)

if (MSVC)
    target_compile_options(music PRIVATE /W4 /permissive- /utf-8)
else ()
    target_compile_options(music PRIVATE -Wall -Wextra -Wpedantic)
endif ()
