# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, You can obtain one at
# https://mozilla.org/MPL/2.0/.

# elg_write_link_manifest(<target> <file> [STATIC_CXX_RUNTIME])
# STATIC_CXX_RUNTIME packages compiler-selected Linux libstdc++/libgcc archives for prebuilts;
# source builds retain the default system C++ runtime.
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
    cmake_parse_arguments(PARSE_ARGV 2 manifest "STATIC_CXX_RUNTIME" "" "")
    if (manifest_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "Unknown link manifest arguments: ${manifest_UNPARSED_ARGUMENTS}")
    endif ()
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

    # CMAKE_CXX_STANDARD_LIBRARIES is usually empty on Unix: the C++ compiler driver
    # supplies its standard library implicitly. Rust/C consumers do not use that driver.
    # Keep runtime inputs after the archives. Source builds use system library names;
    # Linux prebuilt packages can instead carry their compiler's static runtime archives.
    if (APPLE)
        list(APPEND lines "lib c++")
    elseif (CMAKE_SYSTEM_NAME STREQUAL "Linux")
        if (manifest_STATIC_CXX_RUNTIME)
            # Do not silently mix libc++ or compiler-rt with GCC's ABI/unwinder archives.
            # Clang with its default libstdc++/libgcc runtime is supported, too.
            if ("c++" IN_LIST CMAKE_CXX_IMPLICIT_LINK_LIBRARIES
                    OR CMAKE_CXX_FLAGS MATCHES "-stdlib=libc[+][+]"
                    OR CMAKE_CXX_FLAGS MATCHES "-rtlib=compiler-rt"
                    OR CMAKE_CXX_IMPLICIT_LINK_LIBRARIES MATCHES "clang_rt")
                message(FATAL_ERROR "Linux static C API prebuilts require libstdc++ and libgcc. "
                    "Use GCC, or Clang with -stdlib=libstdc++ --rtlib=libgcc; "
                    "libc++/compiler-rt toolchains are not supported for this package.")
            endif ()
            foreach (archive IN ITEMS libstdc++.a libgcc.a libgcc_eh.a libgcc.a)
                _elg_manifest_compiler_archive("${archive}" path)
                list(APPEND lines "static ${path}")
            endforeach ()
            # libgcc_eh can reference builtins in libgcc, hence the second libgcc entry.
        elseif ("c++" IN_LIST CMAKE_CXX_IMPLICIT_LINK_LIBRARIES)
            list(APPEND lines "lib c++")
        else ()
            list(APPEND lines "lib stdc++")
        endif ()
        # The C driver already supplies libc and compiler builtins, but not libm or
        # a separately configured libc++ ABI/unwinder library.
        foreach (runtime IN LISTS CMAKE_CXX_IMPLICIT_LINK_LIBRARIES)
            if (runtime MATCHES "^(m|c[+][+]abi|unwind)$")
                list(APPEND lines "lib ${runtime}")
            endif ()
        endforeach ()
    endif ()

    list(APPEND lines "define $<JOIN:$<TARGET_PROPERTY:${target},INTERFACE_COMPILE_DEFINITIONS>,\ndefine >")
    if (DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
        list(APPEND lines "runtime-dir ${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/$<$<CONFIG:Debug>:debug/>bin")
    endif ()
    string(JOIN "\n" content ${lines})
    file(GENERATE OUTPUT "${file}" CONTENT "${content}\n")
endfunction()

# Ask the selected compiler, not the host's default GCC installation. In particular, GCC 14's
# libstdc++.a must travel with GCC 14-built objects instead of requiring it on the Rust machine.
function(_elg_manifest_compiler_archive name out)
    separate_arguments(compiler_arg1 NATIVE_COMMAND "${CMAKE_CXX_COMPILER_ARG1}")
    separate_arguments(compiler_flags NATIVE_COMMAND "${CMAKE_CXX_FLAGS}")
    set(target_flags "")
    if (CMAKE_CXX_COMPILER_TARGET)
        list(APPEND target_flags "--target=${CMAKE_CXX_COMPILER_TARGET}")
    endif ()
    if (CMAKE_SYSROOT)
        list(APPEND target_flags "--sysroot=${CMAKE_SYSROOT}")
    endif ()
    execute_process(COMMAND "${CMAKE_CXX_COMPILER}" ${compiler_arg1} ${compiler_flags}
            ${target_flags} "-print-file-name=${name}"
            RESULT_VARIABLE status OUTPUT_VARIABLE path ERROR_VARIABLE error
            OUTPUT_STRIP_TRAILING_WHITESPACE TIMEOUT 30)
    if (NOT status EQUAL 0 OR NOT IS_ABSOLUTE "${path}" OR NOT EXISTS "${path}")
        message(FATAL_ERROR "Compiler could not locate static runtime ${name}: ${path} ${error}. "
            "Install the selected compiler's static C++/GCC runtime development archives.")
    endif ()
    set(${out} "${path}" PARENT_SCOPE)
endfunction()

# Depth first: an item's lines go in front of the order once its dependencies are in, so it ends
# up before all of them (reverse post-order). `deps` overrides the item's own (the root's private
# LINK_LIBRARIES).
function(_elg_manifest_visit item deps)
    # Resolve target-valued wrappers before visiting dependencies. Leaving them for
    # file(GENERATE) turns an optional Boost import into a literal "lib Boost::..."
    # line, too late to discover its archive and transitive dependencies.
    while (TRUE)
        if (item MATCHES "^\\$<(LINK_ONLY|BUILD_INTERFACE|BUILD_LOCAL_INTERFACE):(.+)>$")
            set(item "${CMAKE_MATCH_2}")
        elseif (item MATCHES "^\\$<TARGET_NAME_IF_EXISTS:([^<>]+)>$")
            set(item "${CMAKE_MATCH_1}")
            if (NOT TARGET "${item}")
                return()
            endif ()
        elseif (item MATCHES "^\\$<INSTALL_INTERFACE:.*>$")
            return()
        else ()
            break()
        endif ()
    endwhile ()
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
            # Many find modules declare UNKNOWN imports even for static vcpkg libraries. On
            # MSVC their .lib extension cannot distinguish archives from DLL import libraries.
            # Shared targets (notably AccessKit) retain their explicit lib + runtime pair above.
            if (VCPKG_TARGET_TRIPLET MATCHES "-static(-md)?$")
                set(lines "static $<TARGET_LINKER_FILE:${item}>")
            else ()
                set(lines "lib $<TARGET_LINKER_FILE:${item}>")
            endif ()
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
        if (IS_ABSOLUTE "${item}" AND (item MATCHES "\\.a$"
                OR (item MATCHES "\\.lib$" AND VCPKG_TARGET_TRIPLET MATCHES "-static(-md)?$")))
            set(lines "static ${item}")
        else ()
            set(lines "lib ${item}")
        endif ()
    endif ()
    get_property(order GLOBAL PROPERTY _elg_manifest_order)
    list(PREPEND order ${lines})
    set_property(GLOBAL PROPERTY _elg_manifest_order "${order}")
endfunction()
