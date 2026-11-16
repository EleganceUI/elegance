// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Core's backend (core/os_backend.h) on macOS.

#include "core/os_backend.h"

#include <elg/error.h>

#include <dlfcn.h>
#include <mach-o/dyld.h>
#include <sys/sysctl.h>
#include <unistd.h>

#include <algorithm>
#include <cstdint>
#include <csignal>
#include <cstdlib>
#include <format>

#if !defined(__cpp_lib_stacktrace)
#include <execinfo.h>
#endif

namespace elg::detail::os {

std::filesystem::path executable_path() {
    std::uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string buffer(size, '\0');
    if (_NSGetExecutablePath(buffer.data(), &size) == 0) {
        return buffer.c_str();
    }
    return {};
}

int process_id() noexcept {
    return getpid();
}

std::FILE* open_command_pipe(const std::string& command) {
    return popen((command + " 2>&1").c_str(), "r");
}

int close_command_pipe(std::FILE* pipe) {
    return pclose(pipe);
}

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

void* open_library(const std::filesystem::path& path) {
    void* const handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        // glibc and macOS keep dlerror()'s message per thread.
        throw ResourceError(std::format("can't load \"{}\": {}", path.string(), dlerror())); // NOLINT(concurrency-mt-unsafe)
    }
    return handle;
}

void* library_symbol(void* library, const char* name) noexcept {
    return dlsym(library, name);
}

void close_library(void* library) noexcept {
    dlclose(library);
}

} // namespace elg::detail::os
