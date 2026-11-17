// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Core's backend (core/os_backend.h) on Windows. Its standard library has <stacktrace>, so the
// stack functions aren't needed here.

#include "core/os_backend.h"

#include <elg/error.h>

#include <process.h>
#include <windows.h>

#include <cstdio>
#include <format>

namespace elg::detail::os {

std::filesystem::path executable_path() {
    wchar_t buffer[MAX_PATH];
    if (const DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH); length > 0 && length < MAX_PATH) {
        return std::wstring(buffer, length);
    }
    return {};
}

int process_id() noexcept {
    return _getpid();
}

// A command is a pipe from _popen, whose process can't be ended here yet.
void* start_command(const std::string& command) {
    // cmd /c strips the outer quotes of a line that starts with one; keep the command's own.
    return _popen(("\"" + command + " 2>&1\"").c_str(), "r");
}

std::size_t read_command_output(void* command, std::span<char> buffer) {
    return std::fread(buffer.data(), 1, buffer.size(), static_cast<std::FILE*>(command));
}

void end_command(void*) noexcept {}

int finish_command(void* command) noexcept {
    return _pclose(static_cast<std::FILE*>(command));
}

bool threads_end_before_static_destruction() noexcept {
#if defined(ELG_STATIC_DEFINE)
    return false; // linked into the program: exit() destroys them while its threads still run
#else
    return true; // a DLL: unloaded after ExitProcess has ended the other threads
#endif
}

bool debugger_present() noexcept {
    return IsDebuggerPresent() != 0;
}

void debug_break() noexcept {
    __debugbreak();
}

void* open_library(const std::filesystem::path& path) {
    void* const handle = LoadLibraryW(path.c_str());
    if (!handle) {
        throw ResourceError(std::format("can't load \"{}\" (error {})", path.string(), GetLastError()));
    }
    return handle;
}

void* library_symbol(void* library, const char* name) noexcept {
    return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(library), name));
}

void close_library(void* library) noexcept {
    FreeLibrary(static_cast<HMODULE>(library));
}

} // namespace elg::detail::os
