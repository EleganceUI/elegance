# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, You can obtain one at
# https://mozilla.org/MPL/2.0/.

# libFuzzer targets (ELG_FUZZ), one executable per parser of untrusted input. The top-level
# CMakeLists.txt builds everything with AddressSanitizer, UBSan and the fuzzer's coverage
# instrumentation; these link libFuzzer's main. corpus/<target>/ holds each one's seed inputs:
#
#   build/<preset>/bin/fuzz_utf8 -max_total_time=60 new-inputs/ fuzz/corpus/fuzz_utf8/

find_package(boost_beast CONFIG REQUIRED) # fuzz_http_headers parses responses as downloads do

# Distributions build libFuzzer's runtime against libstdc++. With libc++, the targets then link
# libstdc++ as well, for the runtime alone (its symbols are in another namespace than libc++'s).
# It's named by file: Clang's driver turns -lstdc++ into the C++ library it was told to use.
include(CheckCXXSourceCompiles)
set(elg_fuzz_main [=[
    #include <cstddef>
    #include <cstdint>
    extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t*, std::size_t) { return 0; }
]=])
set(CMAKE_REQUIRED_LINK_OPTIONS -fsanitize=fuzzer)
check_cxx_source_compiles("${elg_fuzz_main}" ELG_FUZZER_LINKS)
set(elg_fuzzer_link -fsanitize=fuzzer)
if (NOT ELG_FUZZER_LINKS)
    set(CMAKE_REQUIRED_LINK_OPTIONS -fsanitize=fuzzer -l:libstdc++.so.6)
    check_cxx_source_compiles("${elg_fuzz_main}" ELG_FUZZER_LINKS_WITH_LIBSTDCXX)
    if (NOT ELG_FUZZER_LINKS_WITH_LIBSTDCXX)
        message(FATAL_ERROR "ELG_FUZZ: can't link a libFuzzer target; is compiler-rt (libclang-rt-dev) installed?")
    endif ()
    list(APPEND elg_fuzzer_link -l:libstdc++.so.6)
endif ()
unset(CMAKE_REQUIRED_LINK_OPTIONS)

set(elg_fuzz_targets fuzz_image fuzz_svg_path fuzz_http_headers fuzz_cache_file fuzz_proxy_bypass fuzz_utf8)
foreach (target IN LISTS elg_fuzz_targets)
    add_executable(${target} ${target}.cpp)
    target_link_libraries(${target} PRIVATE Elegance::Elegance)
    target_include_directories(${target} PRIVATE ${PROJECT_SOURCE_DIR}/src)
    target_link_options(${target} PRIVATE ${elg_fuzzer_link})
    elegance_warnings(${target})
endforeach ()
target_link_libraries(fuzz_http_headers PRIVATE Boost::beast)

# `cmake --build ... --target fuzz` builds them all.
add_custom_target(fuzz DEPENDS ${elg_fuzz_targets})
