# Chainloaded by the x64-mikros triplet: Mikros's own toolchain file, which limits where CMake looks for
# packages to the sysroot, plus the vcpkg tree of the triplet, where the dependencies built so far are, and
# the sysroot's pkg-config files in place of the build machine's.
include("$ENV{MIKROS_SYSROOT}/x86_64-unknown-mikros.cmake")
if (DEFINED _VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
    list(APPEND CMAKE_FIND_ROOT_PATH "${_VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}"
         "${_VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug")
endif ()
set(ENV{PKG_CONFIG_LIBDIR} "$ENV{MIKROS_SYSROOT}/lib/pkgconfig:$ENV{MIKROS_SYSROOT}/share/pkgconfig")
set(ENV{PKG_CONFIG_SYSROOT_DIR} "$ENV{MIKROS_SYSROOT}")
