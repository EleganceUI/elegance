# Linux arm64 with Clang and libc++: every dependency is built against libc++ too, so it matches
# Elegance's standard library. libc++ 19 has std::jthread and std::stop_token only under
# -fexperimental-library. cmake/ElgBootstrap.cmake picks this triplet for a -stdlib=libc++ build.
set(VCPKG_TARGET_ARCHITECTURE arm64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)

set(VCPKG_CMAKE_SYSTEM_NAME Linux)
set(VCPKG_ENV_PASSTHROUGH ELG_LIBCXX_CC ELG_LIBCXX_CXX)
set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "${CMAKE_CURRENT_LIST_DIR}/linux-libcxx-toolchain.cmake")
set(VCPKG_CXX_FLAGS "-stdlib=libc++ -fexperimental-library")
set(VCPKG_C_FLAGS "")
set(VCPKG_LINKER_FLAGS "-stdlib=libc++ -fexperimental-library")
