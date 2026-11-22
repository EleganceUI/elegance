# Mikros on x64: every dependency is built with the Clang target and options of Mikros's sysroot, through the
# sysroot's own CMake toolchain file (ELGSPEC-0040.R1). MIKROS_SYSROOT names the sysroot, and the CMake that
# configures the build has to be the one with Mikros's patches (the sysroot's toolchain file says which).
# The libraries are static: they go into eleganceui.<major>.dlib (ELGSPEC-0040.R16).
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)

set(VCPKG_CMAKE_SYSTEM_NAME Mikros)
# The libraries are linked into a shared library, so their code has to be position independent.
set(VCPKG_C_FLAGS "-fPIC")
set(VCPKG_CXX_FLAGS "-fPIC")
# vcpkg has no identifier for Mikros in a manifest's "platform", and none of its systems is true for it: the
# manifests name Mikros as "none of the systems vcpkg knows".
set(VCPKG_ENV_PASSTHROUGH MIKROS_SYSROOT)
if (NOT DEFINED ENV{MIKROS_SYSROOT})
    message(FATAL_ERROR "The x64-mikros triplet needs the environment variable MIKROS_SYSROOT, the path of "
                        "Mikros's sysroot.")
endif ()
set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "${CMAKE_CURRENT_LIST_DIR}/mikros-toolchain.cmake")

# Meson ports (HarfBuzz) run the Meson that Mikros builds next to its sysroot, which names the static libraries as
# CMake looks for them on Mikros (lib<name>.slib), and not the Meson that vcpkg downloads, which names them lib<name>.a.
file(GLOB elg_mikros_meson "$ENV{MIKROS_SYSROOT}/../meson/meson-*/meson.py")
list(SORT elg_mikros_meson)
if (elg_mikros_meson)
    list(GET elg_mikros_meson -1 SCRIPT_MESON)
else ()
    message(FATAL_ERROR "The x64-mikros triplet needs Mikros's Meson, which is in the folder meson beside the "
                        "sysroot's ($ENV{MIKROS_SYSROOT}/../meson).")
endif ()
# They also need the sysroot's Meson file with its folder filled in, and the word that the programs they build for
# Mikros can't run on the build machine.
file(READ "$ENV{MIKROS_SYSROOT}/x86_64-unknown-mikros.meson" elg_meson)
string(REPLACE "@DIRNAME@" "$ENV{MIKROS_SYSROOT}" elg_meson "${elg_meson}")
string(REPLACE "[properties]\n" "[properties]\nneeds_exe_wrapper = true\n" elg_meson "${elg_meson}")
# Its sys_root would make pkg-config put the sysroot's folder before vcpkg's, where the ports' files are.
string(REGEX REPLACE "\nsys_root[^\n]*" "" elg_meson "${elg_meson}")
string(REGEX REPLACE "\npkg_config_libdir[^\n]*" "" elg_meson "${elg_meson}")
# pkg-config is given the sysroot's folder to put before every path it prints, which suits the sysroot's own
# libraries and breaks the ones that vcpkg built (they are in the build's own folder): it runs without it.
string(REPLACE "pkg-config = 'pkg-config'"
       "pkg-config = ['env', '-u', 'PKG_CONFIG_SYSROOT_DIR', '-u', 'PKG_CONFIG_LIBDIR', 'pkg-config']"
       elg_meson "${elg_meson}")
# The libraries are static, as the triplet says; as the cross file is the target's, the option goes in it.
string(APPEND elg_meson "\n[built-in options]\ndefault_library = 'static'\n")
# HarfBuzz's command-line programs link with -l<name>, which Mikros's compiler looks for among the sysroot's
# libraries and so can't find the static ones of vcpkg: they aren't built.
string(APPEND elg_meson "\n[project options]\nutilities = 'disabled'\n")
if (DEFINED ENV{TMPDIR})
    set(elg_meson_folder "$ENV{TMPDIR}")
else ()
    set(elg_meson_folder "/tmp")
endif ()
# One file for each sysroot, as builds with different ones may run at once.
string(MD5 elg_meson_hash "$ENV{MIKROS_SYSROOT}")
set(VCPKG_MESON_CROSS_FILE "${elg_meson_folder}/elegance-x64-mikros-${elg_meson_hash}.meson")
file(WRITE "${VCPKG_MESON_CROSS_FILE}" "${elg_meson}")
