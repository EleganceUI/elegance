// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_CORE_DYNAMIC_LIBRARY_H
#define ELG_CORE_DYNAMIC_LIBRARY_H

#include <filesystem>
#include <string>

namespace elg::detail {

// A loaded shared library (LoadLibrary / dlopen, through core/os_backend.h). Unloads when
// destroyed.
class DynamicLibrary {
public:
    // Throws elg::ResourceError with the system's reason if it can't be loaded.
    explicit DynamicLibrary(const std::filesystem::path& path);
    ~DynamicLibrary();

    DynamicLibrary(const DynamicLibrary&) = delete;
    DynamicLibrary& operator=(const DynamicLibrary&) = delete;
    DynamicLibrary(DynamicLibrary&&) = delete;
    DynamicLibrary& operator=(DynamicLibrary&&) = delete;

    // The exported symbol, or nullptr.
    void* symbol(const char* name) const;

private:
    void* handle_ = nullptr;
};

} // namespace elg::detail

#endif // ELG_CORE_DYNAMIC_LIBRARY_H
