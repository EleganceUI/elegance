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
#
# With -DELG_C_STATIC=ON it builds the static package instead, which Rust apps link into the
# executable: one archive holding the C API, Elegance, and every static dependency, plus the
# shared libraries that stay separate (AccessKit's) and elegance-link.txt, what linking it takes
# (package-static.cmake). Windows triplets must then be *-windows-static-md: Rust links the shared
# C runtime (/MD), and the archive has to match it.
cmake_minimum_required(VERSION 4.3)
project(elegance_c LANGUAGES CXX)

option(ELG_C_STATIC "Build the static package (one archive, for Rust apps) instead of the shared library" OFF)

set(ELEGANCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")

if (MSVC AND ELG_C_STATIC)
    if (NOT VCPKG_TARGET_TRIPLET MATCHES "-static-md$")
        message(FATAL_ERROR "Use a *-windows-static-md vcpkg triplet (got ${VCPKG_TARGET_TRIPLET})")
    endif ()
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
elseif (MSVC)
    if (NOT VCPKG_TARGET_TRIPLET MATCHES "-static$")
        message(FATAL_ERROR "Use a *-windows-static vcpkg triplet (got ${VCPKG_TARGET_TRIPLET})")
    endif ()
    # Set here, not only in Elegance's directory, so the C API uses the same runtime.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")
endif ()
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(ELG_BUILD_SHARED OFF CACHE BOOL "" FORCE)
# Built below as part of the library, not on its own.
set(ELG_C_API OFF CACHE BOOL "" FORCE)
if (ELG_C_STATIC)
    # ELG_API stays empty, and Elegance writes its link manifest (elegance-link.txt).
    set(ELG_STATIC_EXPORTS OFF CACHE BOOL "" FORCE)
    # AccessKit's static library is a Rust staticlib with its own Rust standard library: in an
    # archive linked into a Rust app, its symbols would clash with the app's.
    set(ELG_ACCESSKIT_SHARED ON CACHE BOOL "" FORCE)
else ()
    set(ELG_STATIC_EXPORTS ON CACHE BOOL "" FORCE)
    # Inside a shared library that hides its symbols, AccessKit's Rust standard library can't clash
    # with the app's, so its static library is fine (and keeps this one file).
    set(ELG_ACCESSKIT_SHARED OFF CACHE BOOL "" FORCE)
endif ()
add_subdirectory("${ELEGANCE_ROOT}" elegance)

set(C_API_SOURCES
        core.cpp
        views.cpp
        ext.cpp
        platform.cpp
        styles.cpp
)

if (ELG_C_STATIC)
    add_library(elegance_c_api STATIC ${C_API_SOURCES})
    target_link_libraries(elegance_c_api PRIVATE elegance)
    if (MSVC)
        target_compile_definitions(elegance_c_api PRIVATE NOMINMAX)
        target_compile_options(elegance_c_api PRIVATE /EHsc /permissive- /utf-8 /Zc:__cplusplus)
    endif ()

    set(package "${CMAKE_CURRENT_BINARY_DIR}/package")
    set(cxx_stdlib "")
    if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
        execute_process(COMMAND ${CMAKE_CXX_COMPILER} -print-file-name=libstdc++.a
                        OUTPUT_VARIABLE cxx_stdlib_path OUTPUT_STRIP_TRAILING_WHITESPACE)
        # GNU ar's MRI parser treats the `++` in libstdc++.a as a token boundary on the
        # runner's toolchain. Merge through a plus-free symlink instead.
        set(cxx_stdlib "${CMAKE_CURRENT_BINARY_DIR}/libstdcxx.a")
        if (NOT EXISTS "${cxx_stdlib}" AND NOT IS_SYMLINK "${cxx_stdlib}")
            file(CREATE_LINK "${cxx_stdlib_path}" "${cxx_stdlib}" SYMBOLIC)
        endif ()
    endif ()
    find_program(ELG_LIBTOOL libtool)
    add_custom_command(
            OUTPUT "${package}/elegance-link.txt"
            COMMAND ${CMAKE_COMMAND}
                    "-DMANIFEST=${CMAKE_CURRENT_BINARY_DIR}/elegance/elegance-link.txt"
                    "-DC_API=$<TARGET_FILE:elegance_c_api>"
                    "-DOUT=${package}"
                    "-DSYSTEM=${CMAKE_SYSTEM_NAME}"
                    "-DAR=${CMAKE_AR}"
                    "-DLIBTOOL=${ELG_LIBTOOL}"
                    "-DCXX_STDLIB=${cxx_stdlib}"
                    -P "${CMAKE_CURRENT_SOURCE_DIR}/package-static.cmake"
            DEPENDS elegance_c_api elegance "${CMAKE_CURRENT_SOURCE_DIR}/package-static.cmake"
                    "${CMAKE_CURRENT_BINARY_DIR}/elegance/elegance-link.txt" ${cxx_stdlib}
            COMMENT "Merging the static package"
            VERBATIM)
    add_custom_target(elegance_c ALL DEPENDS "${package}/elegance-link.txt")

    install(DIRECTORY "${package}/" DESTINATION .)
    install(FILES shim.h DESTINATION include/elg-c)
    install(FILES "${ELEGANCE_ROOT}/LICENSE" DESTINATION . RENAME LICENSE-elegance)
    return()
endif ()

add_library(elegance_c SHARED ${C_API_SOURCES})
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
