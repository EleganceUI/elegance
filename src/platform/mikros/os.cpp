// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Core's backend (core/os_backend.h) on Mikros: processes and libraries through the C library, which has
// no /proc, no <execinfo.h> and no POSIX shell to start a build command in. What SDL offers is in
// platform/sdl/os.cpp.

#include "core/os_backend.h"

#include <elg/error.h>

#include <mikros/process.h>

#include <dlfcn.h>
#include <unistd.h>
#include <unwind.h>

#include <algorithm>
#include <csignal>
#include <cstddef>
#include <format>
#include <string>
#include <vector>

namespace elg::detail::os {

std::filesystem::path executable_path() {
    struct mikros_process_info info{};
    if (mikros_process_info(getpid(), &info) != 0) {
        return {};
    }
    return std::filesystem::path(static_cast<const char*>(info.path));
}

int process_id() noexcept {
    return static_cast<int>(getpid());
}

// There's no shell on Mikros that runs a command line the way the build commands of hot reload are written:
// starting one fails, as for a command that can't be started.
void* start_command(const std::string&) {
    return nullptr;
}

std::size_t read_command_output(void*, std::span<char>) {
    return 0;
}

void end_command(void*) noexcept {}

int finish_command(void*) noexcept {
    return -1;
}

bool threads_end_before_static_destruction() noexcept {
    return false;
}

bool begin_elegance_thread() noexcept {
    return false; // nothing to set up
}

void end_elegance_thread(bool) noexcept {}

bool debugger_present() noexcept {
    return false; // Mikros has no debugger that runs in a process
}

void debug_break() noexcept {
    std::raise(SIGTRAP);
}

#if !defined(__cpp_lib_stacktrace)
namespace {

struct Capture {
    void** frames;
    std::size_t size;
    std::size_t count;
};

_Unwind_Reason_Code collect_frame(_Unwind_Context* context, void* argument) {
    auto& capture = *static_cast<Capture*>(argument);
    if (capture.count == capture.size) {
        return _URC_END_OF_STACK;
    }
    capture.frames[capture.count++] = reinterpret_cast<void*>(_Unwind_GetIP(context));
    return _URC_NO_REASON;
}

} // namespace

// Not inlined, so that the first frame is always this function's own, which the caller doesn't want: the
// trace starts at the caller.
[[gnu::noinline]] std::size_t capture_stack(std::span<void*> frames) noexcept {
    Capture capture{frames.data(), frames.size(), 0};
    _Unwind_Backtrace(collect_frame, &capture);
    if (capture.count == 0) {
        return 0;
    }
    std::copy(frames.begin() + 1, frames.begin() + static_cast<std::ptrdiff_t>(capture.count), frames.begin());
    return capture.count - 1;
}

std::vector<std::string> describe_stack(std::span<void* const> frames) {
    std::vector<std::string> lines;
    lines.reserve(frames.size());
    for (void* const frame : frames) {
        Dl_info info{};
        if (dladdr(frame, &info) == 0 || !info.dli_fbase) {
            lines.emplace_back(std::format("{}", frame));
            continue;
        }
        const auto offset = static_cast<std::size_t>(static_cast<const char*>(frame) - static_cast<const char*>(info.dli_fbase));
        std::string line = std::format("{}+{:#x}", info.dli_fname ? info.dli_fname : "?", offset);
        if (info.dli_sname) {
            line += std::format(" ({})", info.dli_sname);
        }
        lines.push_back(std::move(line));
    }
    return lines;
}
#endif

void* open_library(const std::filesystem::path& path) {
    void* const handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
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
