# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, You can obtain one at
# https://mozilla.org/MPL/2.0/.

add_executable(design
        canvas.cpp
        main.cpp
        model.cpp
        panels.cpp
)

target_link_libraries(design PRIVATE Elegance::Elegance)

if (MSVC)
    target_compile_options(design PRIVATE /W4 /permissive- /utf-8)
else ()
    target_compile_options(design PRIVATE -Wall -Wextra -Wpedantic)
endif ()
