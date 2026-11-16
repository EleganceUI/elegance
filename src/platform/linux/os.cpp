// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Core's backend (core/os_backend.h) on Linux and other Unix desktops.

#include "core/os_backend.h"

#include <elg/error.h>

#include <dlfcn.h>
#include <unistd.h>

#include <algorithm>
#include <csignal>
#include <cstdlib>
#include <format>
#include <fstream>

#if !defined(__cpp_lib_stacktrace)
#include <execinfo.h>
#endif

namespace elg::detail::os {

std::filesystem::path executable_path() {
    std::error_code error;
    return std::filesystem::read_symlink("/proc/self/exe", error);
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

bool threads_end_before_static_destruction() noexcept {
    return false;
}

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
