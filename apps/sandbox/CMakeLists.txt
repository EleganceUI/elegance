add_executable(sandbox
        card.cpp
        contacts.cpp
        main.cpp
        todos.cpp
)

target_link_libraries(sandbox PRIVATE Elegance::Elegance)
target_include_directories(sandbox PRIVATE ${PROJECT_SOURCE_DIR}/apps/common) # no_error_dialogs.h
# Sample images (generated; see assets/).
target_compile_definitions(sandbox PRIVATE "ELG_SANDBOX_ASSETS=\"${CMAKE_CURRENT_SOURCE_DIR}/assets\"")

elegance_warnings(sandbox)
if (MSVC)
    target_compile_options(sandbox PRIVATE /utf-8)
endif ()
