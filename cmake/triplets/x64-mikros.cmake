# Mikros on x64: every dependency is built with the Clang target and options of Mikros's sysroot, through the
# sysroot's own CMake toolchain file (ELGSPEC-0040.R1). MIKROS_SYSROOT names the sysroot, and the CMake that
# configures the build has to be the one with Mikros's patches (the sysroot's toolchain file says which).
# The libraries are static: they go into eleganceui.<major>.dlib (ELGSPEC-0040.R16).
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)

set(VCPKG_CMAKE_SYSTEM_NAME Mikros)
set(VCPKG_ENV_PASSTHROUGH MIKROS_SYSROOT)
if (NOT DEFINED ENV{MIKROS_SYSROOT})
    message(FATAL_ERROR "The x64-mikros triplet needs the environment variable MIKROS_SYSROOT, the path of "
                        "Mikros's sysroot.")
endif ()
set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "${CMAKE_CURRENT_LIST_DIR}/mikros-toolchain.cmake")
