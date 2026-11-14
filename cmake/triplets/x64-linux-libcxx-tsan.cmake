# x64-linux-libcxx with ThreadSanitizer: TSan only sees races in code built with it, so the TSan
# presets build the dependencies with it too.
include("${CMAKE_CURRENT_LIST_DIR}/x64-linux-libcxx.cmake")
string(APPEND VCPKG_C_FLAGS " -fsanitize=thread")
string(APPEND VCPKG_CXX_FLAGS " -fsanitize=thread")
string(APPEND VCPKG_LINKER_FLAGS " -fsanitize=thread")
