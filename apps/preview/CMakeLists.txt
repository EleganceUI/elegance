# The preview host and a views module to try it with (design.md §9.6).

elegance_views_module(gallery_views gallery/gallery.cpp)

add_executable(elegance-preview main.cpp)
target_link_libraries(elegance-preview PRIVATE Elegance::Elegance)
elegance_hot_reload(elegance-preview MODULE gallery_views WATCH gallery)

foreach (target IN ITEMS gallery_views elegance-preview)
    if (MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
    else ()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif ()
endforeach ()
