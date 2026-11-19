// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Core's backend (core/os_backend.h) on macOS.

#include "core/os_backend.h"

#include <elg/error.h>

#include <crt_externs.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <mach-o/dyld.h>
#include <poll.h>
#include <spawn.h>
#include <sys/sysctl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <memory>

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

// --- Commands ---

namespace {

// A pipe whose ends child processes don't inherit (only the copies a command is given).
bool make_pipe(int (&ends)[2]) noexcept {
    if (pipe(ends) != 0) {
        return false;
    }
    fcntl(ends[0], F_SETFD, FD_CLOEXEC);
    fcntl(ends[1], F_SETFD, FD_CLOEXEC);
    return true;
}

// A shared library can't use `environ` on macOS.
char** environment() noexcept {
    return *_NSGetEnviron();
}

struct Command {
    pid_t shell = 0;        // also its process group's id
    int output = -1;        // the read end of the pipe the command writes to
    int wake[2] = {-1, -1}; // end_command() writes to wake[1], so a read waiting on wake[0] returns
};

void close_descriptor(int& descriptor) noexcept {
    if (descriptor >= 0) {
        close(descriptor);
        descriptor = -1;
    }
}

} // namespace

void* start_command(const std::string& command) {
    auto started = std::make_unique<Command>();
    int output[2] = {-1, -1};
    if (!make_pipe(output)) {
        return nullptr;
    }
    started->output = output[0];
    if (!make_pipe(started->wake)) {
        close_descriptor(output[1]);
        finish_command(started.release());
        return nullptr;
    }
    // The shell gets the pipe as its standard output and error, and /dev/null as its standard
    // input: in a process group of its own, a read from the terminal would stop it.
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_adddup2(&actions, output[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, output[1], STDERR_FILENO);
    posix_spawnattr_t attributes;
    posix_spawnattr_init(&attributes);
    posix_spawnattr_setpgroup(&attributes, 0);
    posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP);
    std::string shell = "sh";
    std::string dash_c = "-c";
    std::string text = command;
    char* arguments[] = {shell.data(), dash_c.data(), text.data(), nullptr};
    const int error = posix_spawn(&started->shell, "/bin/sh", &actions, &attributes, arguments, environment());
    posix_spawnattr_destroy(&attributes);
    posix_spawn_file_actions_destroy(&actions);
    close_descriptor(output[1]); // the shell has its copies: the output ends when they're closed
    if (error != 0) {
        started->shell = 0;
        finish_command(started.release());
        return nullptr;
    }
    return started.release();
}

std::size_t read_command_output(void* command, std::span<char> buffer) {
    auto& running = *static_cast<Command*>(command);
    pollfd waiting[2] = {{.fd = running.output, .events = POLLIN, .revents = 0},
                         {.fd = running.wake[0], .events = POLLIN, .revents = 0}};
    while (true) {
        if (poll(waiting, 2, -1) < 0) {
            if (errno == EINTR) {
                continue;
            }
            return 0;
        }
        if (waiting[1].revents != 0) {
            return 0; // ended
        }
        if (waiting[0].revents != 0) {
            const ssize_t read_bytes = read(running.output, buffer.data(), buffer.size());
            if (read_bytes < 0 && errno == EINTR) {
                continue;
            }
            return read_bytes > 0 ? static_cast<std::size_t>(read_bytes) : 0;
        }
    }
}

void end_command(void* command) noexcept {
    auto& running = *static_cast<Command*>(command);
    // The shell isn't waited for before finish_command(), so its id still names the group.
    kill(-running.shell, SIGKILL);
    const char byte = 1;
    [[maybe_unused]] const ssize_t written = write(running.wake[1], &byte, 1);
}

int finish_command(void* command) noexcept {
    const std::unique_ptr<Command> running(static_cast<Command*>(command));
    close_descriptor(running->output);
    close_descriptor(running->wake[0]);
    close_descriptor(running->wake[1]);
    if (running->shell <= 0) {
        return -1;
    }
    int status = 0;
    while (waitpid(running->shell, &status, 0) < 0) {
        if (errno != EINTR) {
            return -1;
        }
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

bool threads_end_before_static_destruction() noexcept {
    return false;
}

bool begin_elegance_thread() noexcept {
    return false; // nothing to set up
}

void end_elegance_thread(bool) noexcept {}

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
// Not inlined, so that backtrace()'s first frame is always this function's own, which the caller
// doesn't want: the trace starts at the caller, as when it called backtrace() itself.
[[gnu::noinline]] std::size_t capture_stack(std::span<void*> frames) noexcept {
    const auto count = static_cast<std::size_t>(std::max(backtrace(frames.data(), static_cast<int>(frames.size())), 0));
    if (count == 0) {
        return 0;
    }
    std::copy(frames.begin() + 1, frames.begin() + static_cast<std::ptrdiff_t>(count), frames.begin());
    return count - 1;
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
