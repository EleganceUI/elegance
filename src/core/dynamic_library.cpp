// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include "core/dynamic_library.h"

#include "core/os_backend.h"

namespace elg::detail {

DynamicLibrary::DynamicLibrary(const std::filesystem::path& path) : handle_(os::open_library(path)) {}

DynamicLibrary::~DynamicLibrary() {
    os::close_library(handle_);
}

void* DynamicLibrary::symbol(const char* name) const {
    return os::library_symbol(handle_, name);
}

} // namespace elg::detail
