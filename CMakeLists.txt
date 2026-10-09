cmake_minimum_required(VERSION 4.3)
project(Elegence VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
include(ElegenceViewExtensions)

# Dependencies come from vcpkg.json (manifest mode). In classic mode vcpkg ignores the manifest
# and find_package() fails later with a less helpful message.
if (DEFINED VCPKG_MANIFEST_MODE AND NOT VCPKG_MANIFEST_MODE)
    message(FATAL_ERROR
            "vcpkg is running in classic mode (VCPKG_MANIFEST_MODE=OFF), so the dependencies in "
            "vcpkg.json aren't installed. Configure with a preset from CMakePresets.json / "
            "CMakeUserPresets.json (e.g. `cmake --preset dev-debug`), or in CLion enable the "
            "\"Dev Debug\" profile and turn off vcpkg's integration into other CMake profiles.")
endif ()

option(ELG_BUILD_SHARED "Build Elegence as a shared library" ON)
option(ELG_BUILD_SANDBOX "Build the sandbox test app"         ${PROJECT_IS_TOP_LEVEL})
option(ELG_BUILD_EXAMPLES "Build the example apps (needs the vcpkg \"examples\" feature)" ${PROJECT_IS_TOP_LEVEL})
option(ELG_BUILD_TESTS   "Build the unit tests (needs the vcpkg \"tests\" feature)" ${PROJECT_IS_TOP_LEVEL})

# Put the library and every executable in one folder so the sandbox finds the DLL on Windows.
if (PROJECT_IS_TOP_LEVEL)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
endif ()

add_subdirectory(src)

if (ELG_BUILD_SANDBOX)
    add_subdirectory(apps/sandbox)
endif ()

if (ELG_BUILD_EXAMPLES)
    add_subdirectory(apps/login)
endif ()

if (ELG_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif ()
