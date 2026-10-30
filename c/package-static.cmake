# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, You can obtain one at
# https://mozilla.org/MPL/2.0/.

# Script mode (cmake -P), run by c/CMakeLists.txt with ELG_C_STATIC. Turns the C API's static
# library and Elegance's link manifest (cmake/ElgLinkManifest.cmake) into the static package:
#
#   OUT/lib/libelegance_c.a     the C API, Elegance, and every static dependency, merged
#                               (elegance_c.lib with MSVC)
#   OUT/lib/...                 the shared libraries apps need at run time (AccessKit's)
#   OUT/elegance-link.txt       what linking the archive takes, with paths relative to OUT:
#
#       static lib/<archive>    the merged archive
#       lib <name>              a system library or framework (m, -framework Metal, user32.lib)
#       lib lib/<file>          an import library or shared library in the package
#       runtime lib/<file>      a shared library to ship beside the app
#
# One archive means one place a linker looks for every member: GNU ld rescans a single archive
# until nothing new resolves, so the link order of the dependencies stops mattering.
#
# Inputs: MANIFEST, C_API (the C API's static library), OUT, SYSTEM (CMAKE_SYSTEM_NAME), AR
# (CMAKE_AR: ar, or lib.exe with MSVC), LIBTOOL (Apple's), CXX_STDLIB (Linux: libstdc++.a, merged
# in so apps need no newer libstdc++ than the system's).

foreach (var MANIFEST C_API OUT SYSTEM AR)
    if (NOT DEFINED ${var})
        message(FATAL_ERROR "package-static.cmake: ${var} is not set")
    endif ()
endforeach ()

file(REMOVE_RECURSE "${OUT}")
file(MAKE_DIRECTORY "${OUT}/lib")

# A library file no runtime line claimed: a system shared library (/usr/lib/.../libgio-2.0.so,
# an SDK's libz.tbd), linked by name so the app finds its own copy, or else a static library to
# merge.
function (elg_unclaimed_lib path)
    get_filename_component(name "${path}" NAME)
    if (name MATCHES "^lib(.+)\\.(so(\\.[0-9.]+)?|dylib|tbd)$")
        set(lines ${lines} "lib ${CMAKE_MATCH_1}" PARENT_SCOPE)
    else ()
        set(archives ${archives} "${path}" PARENT_SCOPE)
    endif ()
endfunction ()

file(STRINGS "${MANIFEST}" entries)
set(archives "${C_API}")
set(lines "")
set(previous_lib "")
foreach (entry IN LISTS entries)
    string(STRIP "${entry}" entry)
    if (NOT entry MATCHES "^([a-z-]+) (.*)$")
        continue()
    endif ()
    set(kind "${CMAKE_MATCH_1}")
    string(STRIP "${CMAKE_MATCH_2}" value)
    if (value STREQUAL "")
        continue()
    endif ()

    if (kind STREQUAL "runtime")
        # A shared library: the `lib` line before it is what links it (the import library on
        # Windows, the library itself elsewhere). Both ship in the package.
        get_filename_component(name "${value}" NAME)
        file(COPY "${value}" DESTINATION "${OUT}/lib")
        if (previous_lib)
            get_filename_component(linker_name "${previous_lib}" NAME)
            if (NOT linker_name STREQUAL name)
                file(COPY "${previous_lib}" DESTINATION "${OUT}/lib")
            endif ()
            list(APPEND lines "lib lib/${linker_name}")
            set(previous_lib "")
        endif ()
        list(APPEND lines "runtime lib/${name}")
        continue()
    endif ()
    if (previous_lib)
        elg_unclaimed_lib("${previous_lib}")
        set(previous_lib "")
    endif ()

    if (kind STREQUAL "static")
        get_filename_component(stem "${value}" NAME_WE)
        if (stem MATCHES "^(lib)?accesskit$")
            message(FATAL_ERROR "AccessKit's static library is a Rust staticlib with its own Rust standard "
                                "library, which would clash with a Rust app's: configure with ELG_ACCESSKIT_SHARED")
        endif ()
        list(APPEND archives "${value}")
    elseif (kind STREQUAL "lib")
        if (IS_ABSOLUTE "${value}" AND value MATCHES "/([^/]+)\\.framework/?$")
            list(APPEND lines "lib -framework ${CMAKE_MATCH_1}")
        elseif (IS_ABSOLUTE "${value}")
            # Decided by the next line: a shared library if a runtime line follows.
            set(previous_lib "${value}")
        elseif (value MATCHES "^(-framework |-Wl,-framework,|LINKER:-framework,)(.+)$")
            list(APPEND lines "lib -framework ${CMAKE_MATCH_2}")
        elseif (value MATCHES "^-l(.+)$")
            list(APPEND lines "lib ${CMAKE_MATCH_1}")
        elseif (value STREQUAL "-pthread")
            list(APPEND lines "lib pthread")
        elseif (value MATCHES "^[A-Za-z0-9_.+-]+$" AND NOT value MATCHES "^-")
            list(APPEND lines "lib ${value}")
        else ()
            # A CMake target the manifest couldn't resolve (Boost::context, header-only) or a flag.
            message(STATUS "package-static: skipping `${value}`")
        endif ()
    elseif (kind STREQUAL "runtime-dir")
        # vcpkg's bin folder: with static triplets, only what's loaded at run time (Dawn's DirectX
        # shader compiler on Windows).
        file(GLOB shared "${value}/*.dll" "${value}/*.dylib" "${value}/*.so" "${value}/*.so.*")
        foreach (file IN LISTS shared)
            get_filename_component(name "${file}" NAME)
            file(COPY "${file}" DESTINATION "${OUT}/lib")
            list(APPEND lines "runtime lib/${name}")
        endforeach ()
    endif ()
endforeach ()
if (previous_lib)
    elg_unclaimed_lib("${previous_lib}")
endif ()
list(REMOVE_DUPLICATES archives)
list(REMOVE_DUPLICATES lines)

if (SYSTEM STREQUAL "Darwin")
    set(output "${OUT}/lib/libelegance_c.a")
    list(APPEND lines "lib c++")
    execute_process(COMMAND "${LIBTOOL}" -static -no_warning_for_no_symbols -o "${output}" ${archives}
                    COMMAND_ERROR_IS_FATAL ANY)
elseif (SYSTEM STREQUAL "Windows")
    set(output "${OUT}/lib/elegance_c.lib")
    # Hundreds of long paths: past the command line's length limit.
    list(JOIN archives "\"\n\"" quoted)
    file(WRITE "${OUT}/archives.rsp" "\"${quoted}\"\n")
    execute_process(COMMAND "${AR}" /NOLOGO "/OUT:${output}" "@${OUT}/archives.rsp" COMMAND_ERROR_IS_FATAL ANY)
    file(REMOVE "${OUT}/archives.rsp")
else ()
    set(output "${OUT}/lib/libelegance_c.a")
    if (CXX_STDLIB)
        list(APPEND archives "${CXX_STDLIB}")
    endif ()
    list(APPEND lines "lib m" "lib dl" "lib pthread")
    # ar's MRI scripts merge archives member by member, keeping members that share a name.
    set(script "CREATE ${output}\n")
    foreach (archive IN LISTS archives)
        string(APPEND script "ADDLIB ${archive}\n")
    endforeach ()
    string(APPEND script "SAVE\nEND\n")
    file(WRITE "${OUT}/merge.mri" "${script}")
    execute_process(COMMAND "${AR}" -M INPUT_FILE "${OUT}/merge.mri" COMMAND_ERROR_IS_FATAL ANY)
    file(REMOVE "${OUT}/merge.mri")
endif ()

get_filename_component(output_name "${output}" NAME)
list(PREPEND lines "static lib/${output_name}")
string(JOIN "\n" content ${lines})
file(WRITE "${OUT}/elegance-link.txt" "${content}\n")
list(LENGTH archives count)
message(STATUS "package-static: merged ${count} archives into ${output}")
