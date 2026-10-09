add_executable(sandbox
        contacts.cpp
        main.cpp
        todos.cpp
        view_extensions.cpp
)

# Dot-syntax View extensions (.card(), ...), design.md §9.4.
elegence_view_extensions(sandbox view_extensions.h)

target_link_libraries(sandbox PRIVATE Elegence::Elegence)
# Sample images (generated; see assets/).
target_compile_definitions(sandbox PRIVATE "ELG_SANDBOX_ASSETS=\"${CMAKE_CURRENT_SOURCE_DIR}/assets\"")

if (MSVC)
    target_compile_options(sandbox PRIVATE /W4 /permissive- /utf-8)
else ()
    target_compile_options(sandbox PRIVATE -Wall -Wextra -Wpedantic)
endif ()
