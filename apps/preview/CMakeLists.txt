# The preview host and a views module to try it with (design.md §9.6).

elegence_views_module(gallery_views gallery/gallery.cpp)

add_executable(elegence-preview main.cpp)
target_link_libraries(elegence-preview PRIVATE Elegence::Elegence)
elegence_hot_reload(elegence-preview MODULE gallery_views WATCH gallery)

foreach (target IN ITEMS gallery_views elegence-preview)
    if (MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
    else ()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif ()
endforeach ()
