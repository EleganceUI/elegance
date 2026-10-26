# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, You can obtain one at
# https://mozilla.org/MPL/2.0/.

find_package(Catch2 3 CONFIG REQUIRED)
find_package(Dawn CONFIG REQUIRED)
include(Catch)

add_executable(elegance_tests
        accessibility_tests.cpp
        animation_tests.cpp
        audio_tests.cpp
        capture_tests.cpp
        desktop_tests.cpp
        environment_tests.cpp
        gpu_image_tests.cpp
        grid_tests.cpp
        http_tests.cpp
        image_tests.cpp
        path_tests.cpp
        reactive_tests.cpp
        render_device_lifetime_tests.cpp
        size_observer_tests.cpp
        video_tests.cpp
)
target_link_libraries(elegance_tests PRIVATE Elegance::Elegance Catch2::Catch2WithMain)
target_include_directories(elegance_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_include_directories(elegance_tests SYSTEM PRIVATE
        $<TARGET_PROPERTY:dawn::webgpu_dawn,INTERFACE_INCLUDE_DIRECTORIES>)

elegance_warnings(elegance_tests)
if (MSVC)
    target_compile_options(elegance_tests PRIVATE /utf-8)
endif ()

catch_discover_tests(elegance_tests)

# Compile the public FFI surface as real C, not merely as C++ with extern "C" declarations.
enable_language(C)
add_executable(gpu_image_c_smoke gpu_image_c_smoke.c)
set_target_properties(gpu_image_c_smoke PROPERTIES LINKER_LANGUAGE CXX C_STANDARD 11)
target_link_libraries(gpu_image_c_smoke PRIVATE Elegance::Elegance)
add_test(NAME gpu_image_c_smoke COMMAND gpu_image_c_smoke)
