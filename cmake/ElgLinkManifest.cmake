# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, You can obtain one at
# https://mozilla.org/MPL/2.0/.

# elg_write_link_manifest(<target> <file>)
#
# Writes what linking the static library <target> takes, for builds outside CMake (the Rust
# crate's build script): a static library doesn't carry its dependencies, CMake's targets do.
# One entry per line:
#
#   static <file>        a static library
#   lib <file or name>   a library to link: an import library, or a system library by name
#   runtime <file>       a DLL the app needs beside it at run time
#   runtime-dir <dir>    a folder of such DLLs (vcpkg's, with their own dependencies)
#   define <name[=value]> a compile definition for code that includes the headers
#
# Libraries come in link order, each before the libraries it depends on: GNU ld resolves static
# libraries in one pass, left to right.
function(elg_write_link_manifest target file)
    set_property(GLOBAL PROPERTY _elg_manifest_visited "")
    set_property(GLOBAL PROPERTY _elg_manifest_order "")
    get_target_property(deps ${target} LINK_LIBRARIES)
    _elg_manifest_visit("${target}" "${deps}")
    get_property(lines GLOBAL PROPERTY _elg_manifest_order)

    # The platform libraries CMake links into every C++ program (kernel32, user32, ... on Windows),
    # which the code relies on without naming them.
    separate_arguments(standard NATIVE_COMMAND "${CMAKE_CXX_STANDARD_LIBRARIES}")
    foreach (library IN LISTS standard)
        list(APPEND lines "lib ${library}")
    endforeach ()

    list(APPEND lines "define $<JOIN:$<TARGET_PROPERTY:${target},INTERFACE_COMPILE_DEFINITIONS>,\ndefine >")
    if (DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
        list(APPEND lines "runtime-dir ${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/$<$<CONFIG:Debug>:debug/>bin")
    endif ()
    string(JOIN "\n" content ${lines})
    file(GENERATE OUTPUT "${file}" CONTENT "${content}\n")
endfunction()

# Depth first: an item's lines go in front of the order once its dependencies are in, so it ends
# up before all of them (reverse post-order). `deps` overrides the item's own (the root's private
# LINK_LIBRARIES).
function(_elg_manifest_visit item deps)
    # A static library's private dependencies come as $<LINK_ONLY:...>.
    if (item MATCHES "^\\$<LINK_ONLY:(.+)>$")
        set(item "${CMAKE_MATCH_1}")
    endif ()
    # CMake turns "-framework X" into $<LINK_LIBRARY:FRAMEWORK,X>, which only means something
    # where targets link (file(GENERATE) rejects it): write the flag back. Other link features
    # (WHOLE_ARCHIVE, ...) wrap ordinary libraries: take those as they are.
    if (item MATCHES "^\\$<LINK_LIBRARY:([A-Za-z_]+),(.+)>$")
        set(feature "${CMAKE_MATCH_1}")
        string(REPLACE "," ";" wrapped "${CMAKE_MATCH_2}")
        if (feature STREQUAL "FRAMEWORK" OR feature STREQUAL "WEAK_FRAMEWORK")
            get_property(order GLOBAL PROPERTY _elg_manifest_order)
            foreach (framework IN LISTS wrapped)
                # A path (/System/Library/Frameworks/X.framework) links by its name.
                if (framework MATCHES "([^/]+)\\.framework/?$")
                    set(framework "${CMAKE_MATCH_1}")
                endif ()
                if (NOT "lib -framework ${framework}" IN_LIST order)
                    list(PREPEND order "lib -framework ${framework}")
                endif ()
            endforeach ()
            set_property(GLOBAL PROPERTY _elg_manifest_order "${order}")
        else ()
            list(REVERSE wrapped)
            foreach (library IN LISTS wrapped)
                _elg_manifest_visit("${library}" "")
            endforeach ()
        endif ()
        return()
    endif ()
    if (TARGET "${item}")
        get_target_property(aliased "${item}" ALIASED_TARGET)
        if (aliased)
            set(item "${aliased}")
        endif ()
    endif ()
    get_property(visited GLOBAL PROPERTY _elg_manifest_visited)
    if (NOT item OR item IN_LIST visited)
        return()
    endif ()
    set_property(GLOBAL APPEND PROPERTY _elg_manifest_visited "${item}")

    set(lines "")
    if (TARGET "${item}")
        get_target_property(type "${item}" TYPE)
        if (type STREQUAL "STATIC_LIBRARY")
            set(lines "static $<TARGET_LINKER_FILE:${item}>")
        elseif (type STREQUAL "SHARED_LIBRARY")
            set(lines "lib $<TARGET_LINKER_FILE:${item}>" "runtime $<TARGET_FILE:${item}>")
        elseif (type STREQUAL "UNKNOWN_LIBRARY")
            set(lines "lib $<TARGET_LINKER_FILE:${item}>")
        endif ()
        if (NOT deps)
            get_target_property(deps "${item}" INTERFACE_LINK_LIBRARIES)
        endif ()
        if (deps)
            # Last first: each is prepended, so they keep their order where they don't depend on
            # each other (libraries given as plain files, which carry no dependencies).
            list(REVERSE deps)
            foreach (dep IN LISTS deps)
                _elg_manifest_visit("${dep}" "")
            endforeach ()
        endif ()
    else ()
        # A file, a system library's name, or a generator expression for one.
        set(lines "lib ${item}")
    endif ()
    get_property(order GLOBAL PROPERTY _elg_manifest_order)
    list(PREPEND order ${lines})
    set_property(GLOBAL PROPERTY _elg_manifest_order "${order}")
endfunction()
