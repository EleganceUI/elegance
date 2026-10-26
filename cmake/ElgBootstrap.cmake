# Included before project(), so that `cmake --preset <name>` works on a machine that only has a compiler,
# CMake and git: it finds or clones vcpkg, makes sure the clone knows vcpkg.json's baseline, fetches Ninja
# when it isn't installed, and names missing system tools before vcpkg spends minutes failing on them.
#
# vcpkg comes from, in order: an existing CMAKE_TOOLCHAIN_FILE (CLion's vcpkg integration passes one),
# $VCPKG_ROOT, or a clone this script manages in the user's cache folder (shared between checkouts).

# Only the top-level project picks the toolchain.
if (NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    return()
endif ()

file(READ "${CMAKE_CURRENT_LIST_DIR}/../vcpkg.json" elg_manifest)
string(JSON elg_vcpkg_baseline GET "${elg_manifest}" builtin-baseline)

find_package(Git QUIET)

# Run a command and stop with its output when it fails.
function(elg_bootstrap_run what)
    execute_process(COMMAND ${ARGN} RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE output)
    if (NOT result EQUAL 0)
        message(FATAL_ERROR "Elegance: ${what} failed:\n${output}")
    endif ()
endfunction()

# --- vcpkg -------------------------------------------------------------------------------------------------

set(elg_vcpkg_toolchain_suffix "scripts/buildsystems/vcpkg.cmake")
set(elg_vcpkg_managed OFF)
if (CMAKE_TOOLCHAIN_FILE AND EXISTS "${CMAKE_TOOLCHAIN_FILE}")
    if (CMAKE_TOOLCHAIN_FILE MATCHES "^(.*)/${elg_vcpkg_toolchain_suffix}$")
        set(elg_vcpkg_root "${CMAKE_MATCH_1}")
    endif ()
elseif (DEFINED ENV{VCPKG_ROOT} AND EXISTS "$ENV{VCPKG_ROOT}/${elg_vcpkg_toolchain_suffix}")
    file(TO_CMAKE_PATH "$ENV{VCPKG_ROOT}" elg_vcpkg_root)
else ()
    if (DEFINED ENV{LOCALAPPDATA})
        file(TO_CMAKE_PATH "$ENV{LOCALAPPDATA}/elegance/vcpkg" elg_vcpkg_root)
    elseif (DEFINED ENV{XDG_CACHE_HOME})
        set(elg_vcpkg_root "$ENV{XDG_CACHE_HOME}/elegance/vcpkg")
    else ()
        set(elg_vcpkg_root "$ENV{HOME}/.cache/elegance/vcpkg")
    endif ()
    set(elg_vcpkg_managed ON)
endif ()

if (elg_vcpkg_root)
    # Two presets configuring at once share the managed clone.
    get_filename_component(elg_vcpkg_parent "${elg_vcpkg_root}" DIRECTORY)
    file(MAKE_DIRECTORY "${elg_vcpkg_parent}")
    file(LOCK "${elg_vcpkg_root}.lock" GUARD FILE)

    if (elg_vcpkg_managed AND NOT EXISTS "${elg_vcpkg_root}/.git")
        if (NOT GIT_FOUND)
            message(FATAL_ERROR "Elegance: git is needed to download vcpkg. Install git, or point VCPKG_ROOT at "
                                "an existing vcpkg clone.")
        endif ()
        message(STATUS "Elegance: cloning vcpkg into ${elg_vcpkg_root}")
        elg_bootstrap_run("Cloning vcpkg"
                          "${GIT_EXECUTABLE}" clone https://github.com/microsoft/vcpkg "${elg_vcpkg_root}")
    endif ()

    # vcpkg reads the port versions from the baseline commit, which an older clone doesn't have.
    if (GIT_FOUND AND EXISTS "${elg_vcpkg_root}/.git")
        execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${elg_vcpkg_root}" cat-file -e "${elg_vcpkg_baseline}^{commit}"
                        RESULT_VARIABLE elg_baseline_missing OUTPUT_QUIET ERROR_QUIET)
        if (elg_baseline_missing)
            message(STATUS "Elegance: fetching vcpkg's baseline ${elg_vcpkg_baseline}")
            elg_bootstrap_run("Fetching vcpkg" "${GIT_EXECUTABLE}" -C "${elg_vcpkg_root}" fetch origin)
        endif ()

        # Ports at the baseline can use helpers from newer vcpkg scripts, so the checkout must not be older.
        execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${elg_vcpkg_root}" merge-base --is-ancestor
                                "${elg_vcpkg_baseline}" HEAD
                        RESULT_VARIABLE elg_checkout_older OUTPUT_QUIET ERROR_QUIET)
        if (elg_checkout_older)
            if (elg_vcpkg_managed)
                message(STATUS "Elegance: checking out vcpkg ${elg_vcpkg_baseline}")
                elg_bootstrap_run("Checking out vcpkg's baseline" "${GIT_EXECUTABLE}" -C "${elg_vcpkg_root}"
                                  checkout --quiet --detach "${elg_vcpkg_baseline}")
                file(REMOVE "${elg_vcpkg_root}/vcpkg" "${elg_vcpkg_root}/vcpkg.exe")
            else ()
                message(WARNING "Elegance: the vcpkg clone at ${elg_vcpkg_root} is older than vcpkg.json's baseline. "
                                "If dependencies fail to build, update it:\n"
                                "  git -C \"${elg_vcpkg_root}\" pull\n"
                                "and run its bootstrap-vcpkg script again.")
            endif ()
        endif ()
    endif ()

    if (CMAKE_HOST_WIN32)
        set(elg_vcpkg_exe "${elg_vcpkg_root}/vcpkg.exe")
        set(elg_vcpkg_bootstrap "${elg_vcpkg_root}/bootstrap-vcpkg.bat")
    else ()
        set(elg_vcpkg_exe "${elg_vcpkg_root}/vcpkg")
        set(elg_vcpkg_bootstrap "${elg_vcpkg_root}/bootstrap-vcpkg.sh")
    endif ()
    if (NOT EXISTS "${elg_vcpkg_exe}")
        message(STATUS "Elegance: bootstrapping vcpkg")
        elg_bootstrap_run("Bootstrapping vcpkg" "${elg_vcpkg_bootstrap}" -disableMetrics)
    endif ()

    file(LOCK "${elg_vcpkg_root}.lock" RELEASE)

    # FORCE replaces a broken path a failed configure cached (an unset $VCPKG_ROOT gives /scripts/...).
    set(CMAKE_TOOLCHAIN_FILE "${elg_vcpkg_root}/${elg_vcpkg_toolchain_suffix}" CACHE FILEPATH "" FORCE)
endif ()

# --- Ninja -------------------------------------------------------------------------------------------------

if (CMAKE_GENERATOR MATCHES "Ninja" AND NOT CMAKE_MAKE_PROGRAM)
    find_program(elg_ninja NAMES ninja ninja-build)
    if (NOT elg_ninja AND elg_vcpkg_exe)
        message(STATUS "Elegance: Ninja isn't installed, fetching it with vcpkg")
        execute_process(COMMAND "${elg_vcpkg_exe}" fetch ninja --x-stderr-status
                        RESULT_VARIABLE result OUTPUT_VARIABLE elg_ninja ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)
        if (NOT result EQUAL 0)
            unset(elg_ninja)
        endif ()
    endif ()
    if (NOT elg_ninja)
        message(FATAL_ERROR "Elegance: Ninja wasn't found. Install it (brew install ninja, winget install Ninja-build.Ninja, "
                            "or your package manager's ninja-build).")
    endif ()
    set(CMAKE_MAKE_PROGRAM "${elg_ninja}" CACHE FILEPATH "" FORCE)
endif ()

# --- System tools ------------------------------------------------------------------------------------------

if (CMAKE_HOST_WIN32)
    # Ninja doesn't set up MSVC's environment, CMake then fails with "CMAKE_CXX_COMPILER not set".
    find_program(elg_cl cl)
    if (NOT elg_cl AND CMAKE_GENERATOR MATCHES "Ninja" AND NOT DEFINED ENV{VSCMD_VER})
        message(FATAL_ERROR "Elegance: MSVC's cl.exe isn't on PATH. Configure from a \"x64 Native Tools Command Prompt "
                            "for VS\" (or after running vcvars64.bat); in CLion, use the Visual Studio toolchain.")
    endif ()
else ()
    # vcpkg needs pkg-config to install abseil and others, and can't download it on macOS or Linux.
    find_program(elg_pkg_config NAMES pkg-config pkgconf)
    if (NOT elg_pkg_config)
        if (CMAKE_HOST_APPLE)
            set(elg_install "brew install pkg-config")
        else ()
            set(elg_install "install pkg-config (apt install pkg-config, dnf install pkgconf)")
        endif ()
        message(FATAL_ERROR "Elegance: pkg-config wasn't found, and vcpkg needs it to build the dependencies. "
                            "Run: ${elg_install}")
    endif ()
endif ()
