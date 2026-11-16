// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Core's backend (core/os_backend.h) on Linux and other Unix desktops.

#include "core/os_backend.h"

#include <algorithm>
#include <csignal>
#include <cstdlib>
#include <format>
#include <fstream>

#if !defined(__cpp_lib_stacktrace)
#include <execinfo.h>
#endif

namespace elg::detail::os {

bool debugger_present() noexcept {
    std::ifstream status("/proc/self/status");
    std::string line;
    while (std::getline(status, line)) {
        if (line.starts_with("TracerPid:")) {
            return line.find_first_not_of(" \t0", 10) != std::string::npos;
        }
    }
    return false;
}

void debug_break() noexcept {
    std::raise(SIGTRAP);
}

#if !defined(__cpp_lib_stacktrace)
std::size_t capture_stack(std::span<void*> frames) noexcept {
    return static_cast<std::size_t>(std::max(backtrace(frames.data(), static_cast<int>(frames.size())), 0));
}

std::vector<std::string> describe_stack(std::span<void* const> frames) {
    std::vector<std::string> lines;
    lines.reserve(frames.size());
    char** symbols = backtrace_symbols(frames.data(), static_cast<int>(frames.size()));
    for (std::size_t i = 0; i < frames.size(); ++i) {
        lines.emplace_back(symbols ? std::string(symbols[i]) : std::format("{}", frames[i]));
    }
    std::free(static_cast<void*>(symbols));
    return lines;
}
#endif

} // namespace elg::detail::os
