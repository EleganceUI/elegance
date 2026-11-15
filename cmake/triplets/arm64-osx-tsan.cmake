# arm64-osx with ThreadSanitizer: TSan only sees races in code built with it, so the TSan presets
# build the dependencies with it too.
include("${CMAKE_CURRENT_LIST_DIR}/arm64-osx.cmake")
set(VCPKG_C_FLAGS "-fsanitize=thread")
set(VCPKG_CXX_FLAGS "-fsanitize=thread")
set(VCPKG_LINKER_FLAGS "-fsanitize=thread")
