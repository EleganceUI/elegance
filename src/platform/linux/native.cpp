// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Linux (and other Unix desktops) under SDL (platform/generic/native.h): WebGPU draws into the
// Wayland surface or the X11 window. SDL's password input type turns the IME off on Wayland.

#include "platform/generic/native.h"

#include <SDL3/SDL.h>

#include <csignal>
#include <fstream>
#include <string>
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

bool in_move_size_loop() {
    return false;
}

void detach_ime(SDL_Window*) {}

void set_secure_event_input(bool) {}

void* install_input_monitor(InputSink) {
    return nullptr;
}

void remove_input_monitor(void*) {}

} // namespace native

void wait_for_compositor() {}

bool debugger_present() noexcept {
    std::ifstream status("/proc/self/status");
    std::string line;
    while (std::getline(status, line)) {
        if (line.starts_with("TracerPid:")) {
            return line.find_first_not_of(" \t0", 10) != std::string::npos;
        }
    }
    return false;
}

void debug_break() noexcept {
    std::raise(SIGTRAP);
}

} // namespace elg::detail::platform
