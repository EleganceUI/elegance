# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, You can obtain one at
# https://mozilla.org/MPL/2.0/.

# Script mode (cmake -P), run by c/CMakeLists.txt with ELG_C_STATIC. Turns the C API's static
# library and Elegance's link manifest (cmake/ElgLinkManifest.cmake) into the static package:
#
#   OUT/lib/libelegance_c.a     the C API, Elegance, and every static dependency, merged
#                               (elegance_c.lib with MSVC); on Linux, as one object with the
#                               C++ standard library, whose symbols but the C API's are local
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
# (CMAKE_AR: ar, or lib.exe with MSVC), LIBTOOL (Apple's); on Linux, LD, OBJCOPY and NM (CMake's),
# PYTHON (Python 3), and CXX_STDLIB: the archives of the C++ standard library and its unwinder,
# separated by `|`.

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
        if (stem MATCHES "^(lib)?accesskit(-static)?$")
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
    # One relocatable object in an archive: what the C API reaches of Elegance, its static
    # dependencies, and the C++ standard library it was built with and that library's unwinder
    # (CXX_STDLIB), with every symbol but the C API's local. An app then links it with its own C++
    # standard library, or another copy of this one, and the two never meet. Its COMDAT groups
    # become plain sections, so the linker doesn't keep the app's copy of an inline function in
    # place of the package's.
    foreach (var LD OBJCOPY NM PYTHON)
        if (NOT ${var})
            message(FATAL_ERROR "package-static.cmake: ${var} is not set")
        endif ()
    endforeach ()
    set(output "${OUT}/lib/libelegance_c.a")
    string(REPLACE "|" ";" runtime "${CXX_STDLIB}")
    list(APPEND archives ${runtime})
    list(REMOVE_DUPLICATES archives)
    list(APPEND lines "lib m" "lib dl" "lib pthread")
    # Its roots: the C API's functions, and Elegance's own elg_* ones (elg/gpu_image.h). And
    # __cxa_pure_virtual, which libstdc++'s own objects refer to weakly, so that nothing would
    # bring it in: then the app's C++ standard library would define it.
    set(roots "-u" "__cxa_pure_virtual")
    foreach (archive IN LISTS archives)
        execute_process(COMMAND "${NM}" -g --defined-only "${archive}" OUTPUT_VARIABLE defined ERROR_QUIET)
        string(REGEX MATCHALL " [TDBR] elg_[A-Za-z0-9_]+" found "${defined}")
        foreach (symbol IN LISTS found)
            string(REGEX REPLACE "^ [TDBR] " "" symbol "${symbol}")
            list(APPEND roots "-u" "${symbol}")
        endforeach ()
    endforeach ()
    set(object "${OUT}/elegance_c.o")
    # Common symbols get space (-d), so that they too can be made local.
    execute_process(COMMAND "${LD}" -r --force-group-allocation -d -o "${object}" ${roots} --start-group ${archives}
                            --end-group
                    COMMAND_ERROR_IS_FATAL ANY)
    # GCC's GNU unique symbols, which objcopy leaves global, become plain global ones it localizes.
    execute_process(COMMAND "${PYTHON}" "${CMAKE_CURRENT_LIST_DIR}/unique-to-global.py" "${object}"
                    COMMAND_ERROR_IS_FATAL ANY)
    execute_process(COMMAND "${OBJCOPY}" --wildcard "--keep-global-symbol=elg_*" "${object}" COMMAND_ERROR_IS_FATAL ANY)
    file(REMOVE "${output}")
    execute_process(COMMAND "${AR}" rcs "${output}" "${object}" COMMAND_ERROR_IS_FATAL ANY)
    file(REMOVE "${object}")
endif ()

get_filename_component(output_name "${output}" NAME)
list(PREPEND lines "static lib/${output_name}")
string(JOIN "\n" content ${lines})
file(WRITE "${OUT}/elegance-link.txt" "${content}\n")
list(LENGTH archives count)
message(STATUS "package-static: merged ${count} archives into ${output}")
