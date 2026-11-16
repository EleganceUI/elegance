// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Which library unwinds exceptions, on Linux with libstdc++: libgcc_s, whose unwinder libstdc++
// throws with. libunwind.so.8 (which GStreamer needs) defines the same functions on x86-64, and
// supplies them to the whole process when the loader finds it first (src/CMakeLists.txt).

#include <catch2/catch_test_macros.hpp>

#if defined(__GLIBCXX__)
#include <dlfcn.h>

#include <string_view>

TEST_CASE("exceptions unwind with libgcc_s's unwinder", "[unwind]") {
    // What libstdc++'s throw calls: the first definition in the process's global scope.
    void* const found = dlsym(RTLD_DEFAULT, "_Unwind_RaiseException");
    REQUIRE(found != nullptr);
    Dl_info info{};
    REQUIRE(dladdr(found, &info) != 0);
    const std::string_view library = info.dli_fname != nullptr ? info.dli_fname : "";
    INFO("_Unwind_RaiseException comes from " << library);
    CHECK(library.find("libgcc_s") != std::string_view::npos);
}
#endif
