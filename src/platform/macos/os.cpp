// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Core's backend (core/os_backend.h) on macOS.

#include "core/os_backend.h"

#include <sys/sysctl.h>
#include <unistd.h>

#include <algorithm>
#include <csignal>
#include <cstdlib>
#include <format>

#if !defined(__cpp_lib_stacktrace)
#include <execinfo.h>
#endif

namespace elg::detail::os {

bool debugger_present() noexcept {
    int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PID, getpid()};
    kinfo_proc info{};
    std::size_t size = sizeof(info);
    if (sysctl(mib, 4, &info, &size, nullptr, 0) != 0) {
        return false;
    }
    return (info.kp_proc.p_flag & P_TRACED) != 0;
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
