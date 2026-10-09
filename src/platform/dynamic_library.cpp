// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include "platform/dynamic_library.h"

#include <elg/error.h>

#include <format>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace elg::detail::platform {

DynamicLibrary::DynamicLibrary(const std::filesystem::path& path) {
#if defined(_WIN32)
    handle_ = LoadLibraryW(path.c_str());
    if (!handle_) {
        throw ResourceError(std::format("can't load \"{}\" (error {})", path.string(), GetLastError()));
    }
#else
    handle_ = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!handle_) {
        throw ResourceError(std::format("can't load \"{}\": {}", path.string(), dlerror()));
    }
#endif
}

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
