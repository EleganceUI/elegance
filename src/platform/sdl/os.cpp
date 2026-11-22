// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Core's backend (core/os_backend.h): what SDL offers on every OS. The rest is in
// platform/<os>/os.cpp.

#include "core/os_backend.h"

#include <SDL3/SDL.h>

#include <cerrno>
#include <string_view>
#include <system_error>

namespace elg::detail::os {

std::optional<std::string> environment_variable(const char* name) {
    if (const char* value = SDL_getenv(name)) {
        return std::string(value);
    }
    return std::nullopt;
}

namespace {

struct ThreadStart {
    void (*entry)(void*);
    void* argument;
};

int SDLCALL run_thread(void* start) {
    const ThreadStart thread = *static_cast<ThreadStart*>(start);
    SDL_free(start);
    thread.entry(thread.argument);
    return 0;
}

} // namespace

void* start_thread_with_stack(std::size_t stack_size, void (*entry)(void*), void* argument) {
    // The start is copied by SDL's own allocator: the new thread may run before this call returns.
    auto* start = static_cast<ThreadStart*>(SDL_malloc(sizeof(ThreadStart)));
    if (start == nullptr) {
        throw std::system_error(ENOMEM, std::generic_category(), "a thread");
    }
    *start = {entry, argument};
    const SDL_PropertiesID properties = SDL_CreateProperties();
    SDL_SetPointerProperty(properties, SDL_PROP_THREAD_CREATE_ENTRY_FUNCTION_POINTER,
                           reinterpret_cast<void*>(&run_thread));
    SDL_SetPointerProperty(properties, SDL_PROP_THREAD_CREATE_USERDATA_POINTER, start);
    SDL_SetNumberProperty(properties, SDL_PROP_THREAD_CREATE_STACKSIZE_NUMBER, static_cast<Sint64>(stack_size));
    // SDL's macro for this function casts its own arguments in the C style.
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wold-style-cast"
#endif
    SDL_Thread* thread = SDL_CreateThreadWithProperties(properties);
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    SDL_DestroyProperties(properties);
    if (thread == nullptr) {
        SDL_free(start);
        throw std::system_error(EAGAIN, std::generic_category(), "a thread");
    }
    return thread;
}

void join_thread(void* thread) noexcept {
    SDL_WaitThread(static_cast<SDL_Thread*>(thread), nullptr);
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
