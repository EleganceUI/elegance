# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, You can obtain one at
# https://mozilla.org/MPL/2.0/.

# tracy-inspect, for tools/tracy-check.py (ELGSPEC-0042.R12). Not part of Elegance's build: it
# builds against Tracy's own source, at the version vcpkg.json's baseline gives the client, the
# way Tracy's tracy-csvexport does (its CMake fetches the server's dependencies):
#
#   cmake -S tools/tracy-inspect -B build/tracy-inspect -DTRACY_SOURCE=<tracy-0.14.1> -DCMAKE_BUILD_TYPE=Release
#   cmake --build build/tracy-inspect

cmake_minimum_required(VERSION 3.16)

set(TRACY_SOURCE "" CACHE PATH "Tracy's source tree, at the client's version")
if (NOT EXISTS "${TRACY_SOURCE}/cmake/server.cmake")
    message(FATAL_ERROR "set TRACY_SOURCE to Tracy's source tree (with cmake/server.cmake)")
endif ()

set(NO_STATISTICS OFF) # the zones of each source location
include(${TRACY_SOURCE}/cmake/version.cmake)
set(CMAKE_CXX_STANDARD 20)

project(tracy-inspect LANGUAGES C CXX VERSION ${TRACY_VERSION_STRING})

include(${TRACY_SOURCE}/cmake/config.cmake)
include(${TRACY_SOURCE}/cmake/vendor.cmake)
include(${TRACY_SOURCE}/cmake/server.cmake)

add_executable(tracy-inspect main.cpp)
target_link_libraries(tracy-inspect PRIVATE TracyServer)
install(TARGETS tracy-inspect DESTINATION ${CMAKE_INSTALL_BINDIR})
