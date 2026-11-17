# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, You can obtain one at
# https://mozilla.org/MPL/2.0/.

find_package(Catch2 3 CONFIG REQUIRED)
find_package(Dawn CONFIG REQUIRED)
find_package(boost_beast CONFIG REQUIRED) # fetch_tests.cpp's local server
find_package(OpenSSL REQUIRED)
include(Catch)

add_executable(elegance_tests
        accessibility_tests.cpp
        animation_tests.cpp
        app_error_tests.cpp
        app_thread_tests.cpp
        audio_tests.cpp
        backdrop_tests.cpp
        canvas_tests.cpp
        capture_phase_tests.cpp
        capture_tests.cpp
        desktop_tests.cpp
        environment_tests.cpp
        gpu_memory_tests.cpp
        gpu_image_tests.cpp
        grid_tests.cpp
        hover_tests.cpp
        layout_value_tests.cpp
        leaving_tests.cpp
        lifecycle_tests.cpp
        click_tests.cpp
        raw_input_tests.cpp
        gesture_tests.cpp
        disabled_tests.cpp
        dynamic_children_tests.cpp
        floating_tests.cpp
        float_to_int_tests.cpp
        modifier_shorthand_tests.cpp
        picker_slider_style_tests.cpp
        menu_style_tests.cpp
        selection_tests.cpp
        secure_text_input_tests.cpp
        fetch_tests.cpp
        image_decode_tests.cpp
        image_tests.cpp
        navigation_tests.cpp
        number_input_tests.cpp
        os_tests.cpp
        progress_tests.cpp
        prop_argument_tests.cpp
        pointer_style_tests.cpp
        required_callback_tests.cpp
        separator_tests.cpp
        shape_hit_tests.cpp
        size_observer_tests.cpp
        tab_order_tests.cpp
        text_input_tests.cpp
        toggle_tests.cpp
        utf8_tests.cpp
        video_tests.cpp
        test_setup.cpp
)
target_link_libraries(elegance_tests PRIVATE Elegance::Elegance Catch2::Catch2WithMain Boost::beast OpenSSL::SSL)
target_include_directories(elegance_tests PRIVATE ${PROJECT_SOURCE_DIR}/src ${PROJECT_SOURCE_DIR}/apps/common)
target_include_directories(elegance_tests SYSTEM PRIVATE
        $<TARGET_PROPERTY:dawn::webgpu_dawn,INTERFACE_INCLUDE_DIRECTORIES>)
# The GPU tests call Dawn too. A static Dawn is linked into Elegance, which exports it in a shared
# build on Linux and macOS (linking it again would make a second copy); a shared one (vcpkg's on
# Windows) is a DLL the tests link themselves, since a shared Elegance doesn't pass it on.
get_target_property(elg_dawn_type dawn::webgpu_dawn TYPE)
if (elg_dawn_type STREQUAL "SHARED_LIBRARY")
    target_link_libraries(elegance_tests PRIVATE dawn::webgpu_dawn)
endif ()

elegance_warnings(elegance_tests)
if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(elegance_tests PRIVATE unwinder_tests.cpp)
endif ()
# gpu_image_tests.cpp makes IOSurfaces itself; a static Elegance brought the frameworks along.
if (APPLE)
    target_link_libraries(elegance_tests PRIVATE "-framework CoreFoundation" "-framework IOSurface")
endif ()

# The C API's contract with bindings, tested against its objects (only in builds that compile it).
if (TARGET elegance_c_api)
    target_sources(elegance_tests PRIVATE c_api_tests.cpp)
    target_link_libraries(elegance_tests PRIVATE elegance_c_api)
    target_include_directories(elegance_tests PRIVATE ${PROJECT_SOURCE_DIR}/c)
endif ()
if (MSVC)
    target_compile_options(elegance_tests PRIVATE /utf-8)
endif ()

# Hot reload, which needs a shared Elegance: views modules built as an app's are, for the tests to
# load.
if (ELG_BUILD_SHARED)
    elegance_views_module(hot_reload_first hot_reload/first_module.cpp)
    elegance_views_module(hot_reload_second hot_reload/second_module.cpp)
    elegance_views_module(hot_reload_changed hot_reload/changed_module.cpp)
    target_sources(elegance_tests PRIVATE hot_reload_tests.cpp)
    add_dependencies(elegance_tests hot_reload_first hot_reload_second hot_reload_changed)
    target_compile_definitions(elegance_tests PRIVATE
            ELG_TEST_FIRST_MODULE="$<TARGET_FILE:hot_reload_first>"
            ELG_TEST_SECOND_MODULE="$<TARGET_FILE:hot_reload_second>"
            ELG_TEST_CHANGED_MODULE="$<TARGET_FILE:hot_reload_changed>")
endif ()

# Under LeakSanitizer: fontconfig (loaded from the system on Linux) keeps its caches until exit.
# A test that hangs fails after two minutes, by name, instead of holding the run until CI gives up.
catch_discover_tests(elegance_tests PROPERTIES TIMEOUT 120 ENVIRONMENT_MODIFICATION
                     "LSAN_OPTIONS=string_append::suppressions=${CMAKE_CURRENT_SOURCE_DIR}/lsan_suppressions.txt")

# Each layer's own tests, linked with that layer and the layers below it only, as object files
# (ELGSPEC-0001.R9): they build without the layers above it. So must the layer itself, since every
# object file it needs has to be there: a layer without tests of its own still gets one, which
# shows that it links on its own.
function(elg_layer_tests layer)
    set(target elegance_${layer}_tests)
    add_executable(${target} test_setup.cpp ${ARGN})
    get_target_property(objects elegance_${layer} ELG_OBJECTS)
    target_link_libraries(${target} PRIVATE ${objects} Catch2::Catch2WithMain)
    target_include_directories(${target} PRIVATE ${PROJECT_SOURCE_DIR}/apps/common)
    elegance_warnings(${target})
    if (MSVC)
        target_compile_options(${target} PRIVATE /utf-8)
    endif ()
    catch_discover_tests(${target} PROPERTIES TIMEOUT 120 ENVIRONMENT_MODIFICATION
                         "LSAN_OPTIONS=string_append::suppressions=${CMAKE_CURRENT_SOURCE_DIR}/lsan_suppressions.txt")
endfunction()

elg_layer_tests(core ui_release_tests.cpp ui_task_tests.cpp)
elg_layer_tests(geometry path_tests.cpp)
elg_layer_tests(reactive reactive_tests.cpp)
elg_layer_tests(animation)
elg_layer_tests(text glyph_atlas_tests.cpp)
elg_layer_tests(media sample_ring_tests.cpp)
elg_layer_tests(net)
elg_layer_tests(display)
elg_layer_tests(gpu render_device_lifetime_tests.cpp)
elg_layer_tests(render frame_source_tests.cpp)
elg_layer_tests(platform menu_bar_tests.cpp)

# Compile the public FFI surface as real C, not merely as C++ with extern "C" declarations.
enable_language(C)
add_executable(gpu_image_c_smoke gpu_image_c_smoke.c)
set_target_properties(gpu_image_c_smoke PROPERTIES LINKER_LANGUAGE CXX C_STANDARD 11)
target_link_libraries(gpu_image_c_smoke PRIVATE Elegance::Elegance)
add_test(NAME gpu_image_c_smoke COMMAND gpu_image_c_smoke)

# The C API's header (c/include/elg-c/elegance.h, ELGSPEC-0007.R14) compiled as C11, in a program that
# refers to every function it declares (tests/c_api_functions.c) and links with the library.
if (TARGET elegance_c_api)
    add_executable(c_api_c_tests c_api_c_tests.c c_api_functions.c)
    set_target_properties(c_api_c_tests PROPERTIES LINKER_LANGUAGE CXX C_STANDARD 11 C_STANDARD_REQUIRED ON
                                                   C_EXTENSIONS OFF)
    target_include_directories(c_api_c_tests PRIVATE ${PROJECT_SOURCE_DIR}/c/include)
    target_link_libraries(c_api_c_tests PRIVATE elegance_c_api Elegance::Elegance)
    if (elg_dawn_type STREQUAL "SHARED_LIBRARY")
        target_link_libraries(c_api_c_tests PRIVATE dawn::webgpu_dawn)
    endif ()
    if (MSVC)
        target_compile_options(c_api_c_tests PRIVATE /W4 $<$<BOOL:${ELG_WARNINGS_AS_ERRORS}>:/WX>)
    else ()
        target_compile_options(c_api_c_tests PRIVATE -Wall -Wextra -Wpedantic $<$<BOOL:${ELG_WARNINGS_AS_ERRORS}>:-Werror>)
    endif ()
    add_test(NAME c_api_c_tests COMMAND c_api_c_tests)
endif ()

# The layers' folders, dependencies, backend interfaces, OS code and includes (ELGSPEC-0001.R10):
# fails, naming the file, what it includes and the requirement, when any include breaks
# ELGSPEC-0001.R1 to R8. CI also runs it as a step of its own.
find_package(Python3 COMPONENTS Interpreter)
if (Python3_Interpreter_FOUND)
    add_test(NAME layer_check COMMAND Python3::Interpreter "${Elegance_SOURCE_DIR}/tools/layer-check.py")
else ()
    message(STATUS "Elegance: Python 3 not found: the layer check (tools/layer-check.py) isn't a test")
endif ()
