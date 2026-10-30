# Static C API package for language bindings. The manifest preserves the transitive link order
# and installs every non-system library beside the C API and Elegance archives.
#
#   cmake -S c -B build/c -G Ninja -DCMAKE_BUILD_TYPE=Release \
#         -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake \
#         -DVCPKG_MANIFEST_DIR=. -DVCPKG_TARGET_TRIPLET=<triplet>
#   cmake --build build/c
#   cmake --install build/c --prefix <dir>
#
# Windows uses *-windows-static-md: static dependencies, but the shared CRT (/MD), as Rust does.
cmake_minimum_required(VERSION 4.3)
project(elegance_c LANGUAGES CXX)

set(ELEGANCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")

if (MSVC)
    if (NOT VCPKG_TARGET_TRIPLET MATCHES "-windows-static-md$")
        message(FATAL_ERROR "Use a *-windows-static-md vcpkg triplet (got ${VCPKG_TARGET_TRIPLET})")
    endif ()
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif ()
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(ELG_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(ELG_STATIC_EXPORTS OFF CACHE BOOL "" FORCE)
set(ELG_C_API OFF CACHE BOOL "" FORCE)
# AccessKit's Rust staticlib includes its own standard library and clashes with Rust consumers.
set(ELG_ACCESSKIT_SHARED ON CACHE BOOL "" FORCE)

# Declare the shim before configuring Elegance so src/ can traverse it while its
# directory-scoped imported dependencies (SDL, Dawn, ...) are still visible.
add_library(elegance_c STATIC core.cpp views.cpp ext.cpp platform.cpp styles.cpp)
target_link_libraries(elegance_c PRIVATE elegance)
set(ELG_C_PACKAGE_MANIFEST "${CMAKE_CURRENT_BINARY_DIR}/elegance-link-$<CONFIG>.txt")
add_subdirectory("${ELEGANCE_ROOT}" elegance)
if (MSVC)
    target_compile_definitions(elegance_c PRIVATE NOMINMAX)
    target_compile_options(elegance_c PRIVATE /EHsc /permissive- /utf-8 /Zc:__cplusplus /bigobj)
endif ()


set(ELG_PACKAGE_MANIFEST "${CMAKE_CURRENT_BINARY_DIR}/elegance-link-\${CMAKE_INSTALL_CONFIG_NAME}.txt")
configure_file("${ELEGANCE_ROOT}/cmake/ElgInstallLinkManifest.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/ElgInstallLinkManifest.cmake" @ONLY)
install(SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/ElgInstallLinkManifest.cmake")
install(FILES shim.h DESTINATION include/elg-c)
install(FILES "${ELEGANCE_ROOT}/LICENSE" DESTINATION . RENAME LICENSE-elegance)
