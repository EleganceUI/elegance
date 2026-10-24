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
function(elg_write_link_manifest target file)
    set(lines "static $<TARGET_LINKER_FILE:${target}>")
    get_target_property(queue ${target} LINK_LIBRARIES)
    set(visited "")
    while (queue)
        list(POP_FRONT queue item)
        # A static library's private dependencies come as $<LINK_ONLY:...>.
        if (item MATCHES "^\\$<LINK_ONLY:(.+)>$")
            set(item "${CMAKE_MATCH_1}")
        endif ()
        if (TARGET "${item}")
            get_target_property(aliased "${item}" ALIASED_TARGET)
            if (aliased)
                set(item "${aliased}")
            endif ()
        endif ()
        if (NOT item OR item IN_LIST visited)
            continue()
        endif ()
        list(APPEND visited "${item}")

        if (TARGET "${item}")
            get_target_property(type "${item}" TYPE)
            if (type STREQUAL "STATIC_LIBRARY")
                list(APPEND lines "static $<TARGET_LINKER_FILE:${item}>")
            elseif (type STREQUAL "SHARED_LIBRARY")
                list(APPEND lines "lib $<TARGET_LINKER_FILE:${item}>" "runtime $<TARGET_FILE:${item}>")
            elseif (type STREQUAL "UNKNOWN_LIBRARY")
                list(APPEND lines "lib $<TARGET_LINKER_FILE:${item}>")
            endif ()
            get_target_property(deps "${item}" INTERFACE_LINK_LIBRARIES)
            if (deps)
                list(APPEND queue ${deps})
            endif ()
        else ()
            # A file, a system library's name, or a generator expression for one.
            list(APPEND lines "lib ${item}")
        endif ()
    endwhile ()

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
