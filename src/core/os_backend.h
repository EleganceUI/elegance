// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_CORE_OS_BACKEND_H
#define ELG_CORE_OS_BACKEND_H

// Core's backend interface (ELGSPEC-0001.R4): the operating system's utilities. What SDL offers
// on every OS is implemented once, in platform/sdl/os.cpp; the rest in each OS's
// platform/<os>/os.cpp.

#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>
#include <version>

namespace elg::detail::os {

// An environment variable's value, or nullopt when it isn't set.
std::optional<std::string> environment_variable(const char* name);

// Per-user writable folder for Elegance's data (crash reports).
std::filesystem::path pref_path();

// --- Processes ---

// The running program's file, or an empty path if the system doesn't say.
std::filesystem::path executable_path();
int process_id() noexcept;
// Starts `command` in the system's shell, as a child process, its standard error joined to its
// standard output, and returns a handle to it, or nullptr if it can't be started. On Linux and
// macOS the shell leads a process group of its own, which every process it starts joins, and
// reads its standard input from /dev/null. On Windows cmd.exe runs it in a job object of its own,
// which every process it starts joins, with NUL as its standard input.
void* start_command(const std::string& command);
// Reads the command's output into `buffer`, waiting until there's some: how many bytes were read,
// 0 once the output has ended (every process that could write it has exited) or end_command() was
// called.
std::size_t read_command_output(void* command, std::span<char> buffer);
// Kills the command's process group (Linux and macOS) or job (Windows): its shell and what that
// started, without waiting for them; a read_command_output() waiting on another thread returns 0.
// Any thread, until finish_command(), but not at the same time as it.
void end_command(void* command) noexcept;
// Waits for the shell to exit and frees the handle: the shell's exit status, or -1 if it didn't
// exit by itself (end_command() killed it).
int finish_command(void* command) noexcept;
// Whether the process's other threads are already gone when Elegance's static objects are
// destroyed at exit: for a DLL on Windows, which the process unloads after ending them.
bool threads_end_before_static_destruction() noexcept;

// --- Threads that Elegance starts (core/threads.h) ---

// Sets the calling thread up for the system services Elegance's threads use (Windows: COM, in the
// multithreaded apartment): true if end_elegance_thread() has something to undo.
bool begin_elegance_thread() noexcept;
// Undoes begin_elegance_thread(), on the same thread, as it ends.
void end_elegance_thread(bool begun) noexcept;

// --- Crash reports ---

bool debugger_present() noexcept;
// Stops in the attached debugger.
void debug_break() noexcept;
// Shows `message` in a dialog, and returns once it's closed.
void show_error_box(const char* title, const char* message) noexcept;

#if !defined(__cpp_lib_stacktrace)
// Without <stacktrace> (libc++): the calling thread's return addresses, innermost first, written
// to `frames`. Returns how many were written.
std::size_t capture_stack(std::span<void*> frames) noexcept;
// A line for each frame: its symbol, or its address when the system can't name it.
std::vector<std::string> describe_stack(std::span<void* const> frames);
#endif

// --- Shared libraries (core/dynamic_library.h) ---

// Loads the shared library at `path`, returning its handle. Throws elg::ResourceError with the
// system's reason if it can't be loaded.
void* open_library(const std::filesystem::path& path);
// The library's exported symbol `name`, or nullptr.
void* library_symbol(void* library, const char* name) noexcept;
void close_library(void* library) noexcept;

} // namespace elg::detail::os

#endif // ELG_CORE_OS_BACKEND_H
