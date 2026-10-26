cmake_minimum_required(VERSION 4.3)
include(cmake/ElgBootstrap.cmake)
project(Elegance VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# C++23 mode alone does not guarantee the standard library provides these APIs (Xcode 16).
include(CheckCXXSourceCompiles)
check_cxx_source_compiles([=[
    #include <condition_variable>
    #include <mutex>
    #include <stop_token>
    #include <thread>
    int main() {
        std::condition_variable_any ready;
        std::mutex mutex;
        std::jthread worker([&](std::stop_token stop) {
            std::unique_lock lock(mutex);
            ready.wait(lock, stop, [] { return false; });
        });
        worker.request_stop();
    }
]=] ELG_HAVE_JTHREAD)
if (NOT ELG_HAVE_JTHREAD)
    message(FATAL_ERROR
        "Elegance requires std::jthread, std::stop_token, and stop-aware condition variables. "
        "On macOS, select Xcode 26 or later with xcode-select (Xcode 16's libc++ is insufficient).")
endif ()

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
include(ElgWarnings)
include(EleganceViewExtensions)
include(EleganceHotReload)
include(EleganceApplication)

# Dependencies come from vcpkg.json (manifest mode). In classic mode vcpkg ignores the manifest
# and find_package() fails later with a less helpful message.
if (DEFINED VCPKG_MANIFEST_MODE AND NOT VCPKG_MANIFEST_MODE)
    message(FATAL_ERROR
            "vcpkg is running in classic mode (VCPKG_MANIFEST_MODE=OFF), so the dependencies in "
            "vcpkg.json aren't installed. Configure with a preset from CMakePresets.json / "
            "CMakeUserPresets.json (e.g. `cmake --preset windows-debug`), or in CLion enable a preset "
            "profile and turn off vcpkg's integration into other CMake profiles.")
endif ()

# vcpkg's *-static triplets (x64-windows-static) link every dependency statically, the C runtime
# included (/MT): an app is then one executable. Elegance and the apps use the same runtime, and
# Elegance is static too: its API passes std types, which a DLL with its own runtime can't share.
# (*-static-md triplets keep the shared runtime.)
set(ELG_STATIC_RUNTIME OFF)
if (MSVC AND VCPKG_TARGET_TRIPLET MATCHES "-static$")
    set(ELG_STATIC_RUNTIME ON)
    if (NOT DEFINED CMAKE_MSVC_RUNTIME_LIBRARY)
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif ()
endif ()

if (ELG_STATIC_RUNTIME)
    set(elg_shared_default OFF)
else ()
    set(elg_shared_default ON)
endif ()
option(ELG_BUILD_SHARED "Build Elegance as a shared library (OFF: static, linked into each app; no hot reload)"
       ${elg_shared_default})
if (ELG_STATIC_RUNTIME AND ELG_BUILD_SHARED)
    message(FATAL_ERROR "The ${VCPKG_TARGET_TRIPLET} triplet links the C runtime statically, so Elegance must be "
                        "static too (ELG_BUILD_SHARED=OFF): its API passes std types between modules.")
endif ()
option(ELG_BUILD_SANDBOX "Build the sandbox test app"         ${PROJECT_IS_TOP_LEVEL})
option(ELG_ICONS         "Embed the built-in icon set (Phosphor, MIT): regular, bold, fill (~1.4 MB)" ON)
option(ELG_ICONS_EXTRA_WEIGHTS "Also embed Phosphor's thin, light, and duotone styles (~1.6 MB)" ON)
option(ELG_BUILD_TESTS   "Build the unit tests (needs the vcpkg \"tests\" feature)" ${PROJECT_IS_TOP_LEVEL})
option(ELG_ACCESSIBILITY "Screen reader support through AccessKit (downloads its prebuilt C library)" ON)
option(ELG_ACCESSKIT_SHARED "Link AccessKit's shared library instead of its static one (needed by Rust apps)" OFF)
option(ELG_STATIC_EXPORTS "Static build only: keep ELG_API exported, for linking into a shared library that re-exports it"
       OFF)

# Put the library and every executable in one folder so the apps find the DLLs on Windows.
if (PROJECT_IS_TOP_LEVEL)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
endif ()

add_subdirectory(src)

if (ELG_BUILD_SANDBOX)
    add_subdirectory(apps/sandbox)
    if (ELG_BUILD_SHARED) # the hot reload host
        add_subdirectory(apps/preview)
    endif ()
endif ()

if (ELG_BUILD_TESTS AND ELG_BUILD_SHARED)
    # A shared library hides the internals they test (elg::detail).
    message(STATUS "Elegance: skipping the unit tests, which need a static build (ELG_BUILD_SHARED=OFF)")
elseif (ELG_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif ()
