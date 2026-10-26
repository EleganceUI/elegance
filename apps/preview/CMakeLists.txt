# The preview host and a views module to try it with (design.md §9.6).

elegance_views_module(gallery_views gallery/gallery.cpp)

add_executable(elegance-preview main.cpp)
target_link_libraries(elegance-preview PRIVATE Elegance::Elegance)
elegance_hot_reload(elegance-preview MODULE gallery_views WATCH gallery)

elegance_warnings(elegance-preview) # elegance_views_module() applies them to the module
if (MSVC)
    foreach (target IN ITEMS gallery_views elegance-preview)
        target_compile_options(${target} PRIVATE /utf-8)
    endforeach ()
endif ()
