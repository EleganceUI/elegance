// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Linux (and other Unix desktops) under SDL (platform/sdl/native.h): WebGPU draws into the
// Wayland surface or the X11 window. SDL's password input type turns the IME off on Wayland.

#include "platform/sdl/native.h"

#include <elg/events.h>
#include <elg/platform.h>

#include <SDL3/SDL.h>

#include <sys/syscall.h>
#include <unistd.h>

#include <string_view>

namespace elg::detail::platform {

namespace native {

std::uint64_t gpu_window_flags() {
    return 0;
}

WindowParts window_created(SDL_Window*, const WindowDesc&, bool) {
    return {};
}

void window_destroying(SDL_Window*, WindowParts&) {}

SurfaceSource surface_source(SDL_Window* window, const WindowParts&) {
    const SDL_PropertiesID properties = SDL_GetWindowProperties(window);
    SurfaceSource source;
    if (std::string_view(SDL_GetCurrentVideoDriver()) == "wayland") {
        source.kind = SurfaceSource::Kind::wayland;
        source.display = SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, nullptr);
        source.handle = SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, nullptr);
    } else {
        source.kind = SurfaceSource::Kind::xlib;
        source.display = SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr);
        source.xlib_window =
            static_cast<std::uint64_t>(SDL_GetNumberProperty(properties, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0));
    }
    return source;
}

void* native_handle(SDL_Window*) {
    return nullptr;
}

Rect system_controls_frame(WindowParts&) {
    return {};
}

bool on_main_thread() {
    return syscall(SYS_gettid) == getpid(); // the main thread's id is the process's
}

bool in_move_size_loop() {
    return false;
}

void detach_ime(SDL_Window*) {}

void require_tray_icons() {}

void set_secure_event_input(bool) {}

void* install_input_monitor(const InputSink&) {
    return nullptr;
}

void remove_input_monitor(void*) {}

bool begin_run() {
    return false; // nothing to set up
}

void end_run(bool) noexcept {}

} // namespace native

void wait_for_compositor() {}

bool presents_unchanged_exposes() {
    return false;
}

std::optional<std::filesystem::path> caption_icon_font() {
    return std::nullopt;
}

KeyModifiers word_modifier() noexcept {
    return KeyModifiers::control;
}

} // namespace elg::detail::platform

namespace elg {

Platform current_platform() noexcept {
    return Platform::linux;
}

KeyModifiers primary_modifier() noexcept {
    return KeyModifiers::control;
}

} // namespace elg
