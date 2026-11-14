# Chainloaded by the *-linux-libcxx triplets: builds a dependency with the compilers the Elegance
# build uses (cmake/ElgBootstrap.cmake passes them in ELG_LIBCXX_CC and ELG_LIBCXX_CXX), then
# applies vcpkg's regular Linux toolchain.
if (NOT DEFINED ENV{ELG_LIBCXX_CC} OR NOT DEFINED ENV{ELG_LIBCXX_CXX})
    message(FATAL_ERROR "The *-linux-libcxx triplets are for Elegance's build, which sets ELG_LIBCXX_CC and "
                        "ELG_LIBCXX_CXX: configure Elegance with Clang and -stdlib=libc++ instead.")
endif ()
set(CMAKE_C_COMPILER "$ENV{ELG_LIBCXX_CC}")
set(CMAKE_CXX_COMPILER "$ENV{ELG_LIBCXX_CXX}")
if (DEFINED _VCPKG_ROOT_DIR)
    include("${_VCPKG_ROOT_DIR}/scripts/toolchains/linux.cmake")
else ()
    include("${Z_VCPKG_ROOT_DIR}/scripts/toolchains/linux.cmake")
endif ()
