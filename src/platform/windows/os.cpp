// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Core's backend (core/os_backend.h) on Windows. Its standard library has <stacktrace>, so the
// stack functions aren't needed here.

#include "core/os_backend.h"

#include <elg/error.h>

#include <windows.h>

#include <format>

namespace elg::detail::os {

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
