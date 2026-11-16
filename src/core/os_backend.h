// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_CORE_OS_BACKEND_H
#define ELG_CORE_OS_BACKEND_H

// Core's backend interface (ELGSPEC-0001.R4): the operating system's utilities. What SDL offers
// on every OS is implemented once, in platform/sdl/os.cpp; the rest in each OS's
// platform/<os>/os.cpp.

#include <cstddef>
#include <cstdio>
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

int process_id() noexcept;
// Starts `command` in the system's shell, its standard error joined to its standard output, and
// returns a pipe to read that output from, or nullptr if it can't be started.
std::FILE* open_command_pipe(const std::string& command);
// Closes a pipe from open_command_pipe() once the command has ended: the shell's exit status.
int close_command_pipe(std::FILE* pipe);

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
