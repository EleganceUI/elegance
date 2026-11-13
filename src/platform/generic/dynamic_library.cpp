// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include "platform/generic/dynamic_library.h"

#include <elg/error.h>

#include <format>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace elg::detail::platform {

namespace {

void* open_library(const std::filesystem::path& path) {
#if defined(_WIN32)
    void* const handle = LoadLibraryW(path.c_str());
    if (!handle) {
        throw ResourceError(std::format("can't load \"{}\" (error {})", path.string(), GetLastError()));
    }
#else
    void* const handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        // glibc and macOS keep dlerror()'s message per thread.
        throw ResourceError(std::format("can't load \"{}\": {}", path.string(), dlerror())); // NOLINT(concurrency-mt-unsafe)
    }
#endif
    return handle;
}

} // namespace

DynamicLibrary::DynamicLibrary(const std::filesystem::path& path) : handle_(open_library(path)) {}

DynamicLibrary::~DynamicLibrary() {
#if defined(_WIN32)
    FreeLibrary(static_cast<HMODULE>(handle_));
#else
    dlclose(handle_);
#endif
}

void* DynamicLibrary::symbol(const char* name) const {
#if defined(_WIN32)
    return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(handle_), name));
#else
    return dlsym(handle_, name);
#endif
}

} // namespace elg::detail::platform
