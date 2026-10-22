# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, You can obtain one at
# https://mozilla.org/MPL/2.0/.

# Small, self-contained examples, one folder each. The documentation (docs/) shows their
# sources, so they're kept short and use only the public API.

function(elg_example name)
    add_executable(example_${name} ${name}/main.cpp)
    target_link_libraries(example_${name} PRIVATE Elegence::Elegence)
    set_target_properties(example_${name} PROPERTIES OUTPUT_NAME ${name} FOLDER examples)
    if (MSVC)
        target_compile_options(example_${name} PRIVATE /W4 /permissive- /utf-8)
    else ()
        target_compile_options(example_${name} PRIVATE -Wall -Wextra -Wpedantic)
    endif ()
endfunction()

elg_example(hello)
elg_example(counter)
elg_example(todos)
elg_example(layout)
elg_example(styling)
elg_example(drawing)
elg_example(animation)
elg_example(title_bar)
elg_example(media_player)
elg_example(network)
elg_example(recorder)
elg_example(desktop)
elg_example(popover)
elg_example(navigation)
