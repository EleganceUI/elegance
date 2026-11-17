// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Core's backend (core/os_backend.h): what SDL offers on every OS. The rest is in
// platform/<os>/os.cpp.

#include "core/os_backend.h"

#include <SDL3/SDL.h>

#include <string_view>

namespace elg::detail::os {

std::optional<std::string> environment_variable(const char* name) {
    if (const char* value = SDL_getenv(name)) {
        return std::string(value);
    }
    return std::nullopt;
}

std::filesystem::path pref_path() {
    char* path = SDL_GetPrefPath("Elegance", "Elegance");
    if (!path) {
        return std::filesystem::temp_directory_path() / "elegance";
    }
    std::filesystem::path result(std::u8string_view(reinterpret_cast<const char8_t*>(path)));
    SDL_free(path);
    return result;
}

void show_error_box(const char* title, const char* message) noexcept {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title, message, nullptr);
}

} // namespace elg::detail::os
