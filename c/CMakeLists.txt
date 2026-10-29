# The prebuilt C library: the C API (c/*.cpp) and a static Elegance, with all of Elegance's
# dependencies, in one shared library that exports only the C API (shim.h and ELG_API's C
# functions). Language bindings link it with no C++ compiler, CMake, or vcpkg: CI
# (.github/workflows/c-api.yml) builds it per target for each commit on main and attaches it to a
# prerelease tagged c-<first 12 hex digits of the commit>, and elegance-rs's build script downloads
# the one for the commit it pins.
#
#   cmake -S c -B build/c -G Ninja -DCMAKE_BUILD_TYPE=Release \
#         -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake \
#         -DVCPKG_MANIFEST_DIR=. -DVCPKG_TARGET_TRIPLET=<triplet>
#   cmake --build build/c
#   cmake --install build/c --prefix <dir>
#
# Windows triplets must be *-windows-static: the library carries its own C runtime (/MT), so apps
# need no Visual C++ redistributable. On Linux it links libstdc++ statically, so it only needs glibc.
cmake_minimum_required(VERSION 4.3)
project(elegance_c LANGUAGES CXX)

set(ELEGANCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")

if (MSVC)
    if (NOT VCPKG_TARGET_TRIPLET MATCHES "-static$")
        message(FATAL_ERROR "Use a *-windows-static vcpkg triplet (got ${VCPKG_TARGET_TRIPLET})")
    endif ()
    # Set here, not only in Elegance's directory, so the C API uses the same runtime.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")
endif ()
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(ELG_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(ELG_STATIC_EXPORTS ON CACHE BOOL "" FORCE)
# Built below as part of the library, not on its own.
set(ELG_C_API OFF CACHE BOOL "" FORCE)
# Inside a shared library that hides its symbols, AccessKit's Rust standard library can't clash
# with the app's, so its static library is fine (and keeps this one file).
set(ELG_ACCESSKIT_SHARED OFF CACHE BOOL "" FORCE)
add_subdirectory("${ELEGANCE_ROOT}" elegance)

add_library(elegance_c SHARED
        core.cpp
        views.cpp
        ext.cpp
        platform.cpp
        styles.cpp
)
# The whole archive, so ELG_API functions the C API doesn't call (elg_gpu_image_*) are exported too.
target_link_libraries(elegance_c PRIVATE "$<LINK_LIBRARY:WHOLE_ARCHIVE,elegance>")
target_compile_definitions(elegance_c PRIVATE ELG_SHIM_SHARED)
set_target_properties(elegance_c PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON
)
if (MSVC)
    target_compile_definitions(elegance_c PRIVATE NOMINMAX)
    target_compile_options(elegance_c PRIVATE /EHsc /permissive- /utf-8 /Zc:__cplusplus)
elseif (APPLE)
    set_target_properties(elegance_c PROPERTIES INSTALL_NAME_DIR "@rpath")
    # Export only the C API: the dependencies (Abseil, AccessKit's Rust standard library, ...)
    # aren't built with hidden visibility. Leave room for bindings to rewrite the install name.
    target_link_options(elegance_c PRIVATE
            "-Wl,-exported_symbol,_elg_*" -Wl,-headerpad_max_install_names -Wl,-dead_strip)
else ()
    # Carry libstdc++ and libgcc; export only the C API.
    target_link_options(elegance_c PRIVATE
            -static-libstdc++ -static-libgcc "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/exports.map"
            -Wl,--gc-sections -Wl,-z,defs)
    set_property(TARGET elegance_c APPEND PROPERTY LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/exports.map")
endif ()

include(GNUInstallDirs)
install(TARGETS elegance_c
        RUNTIME DESTINATION lib
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib)
install(FILES shim.h DESTINATION include/elg-c)
# Dawn's Direct3D 12 backend loads the DirectX shader compiler at run time.
if (WIN32)
    install(FILES
            "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin/dxcompiler.dll"
            "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin/dxil.dll"
            DESTINATION lib OPTIONAL)
endif ()
install(FILES "${ELEGANCE_ROOT}/LICENSE" DESTINATION . RENAME LICENSE-elegance)
