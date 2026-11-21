// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Mikros under SDL (platform/sdl/native.h): SDL's Wayland video driver over the system's libwayland, and
// WebGPU drawing into the window's wl_surface (ELGSPEC-0040.R3, ELGSPEC-0040.R5).

#include "platform/sdl/native.h"

#include <elg/events.h>
#include <elg/platform.h>

#include <SDL3/SDL.h>

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
    source.kind = SurfaceSource::Kind::wayland;
    source.display = SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, nullptr);
    source.handle = SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, nullptr);
    return source;
}

void* native_handle(SDL_Window*) {
    return nullptr;
}

Rect system_controls_frame(WindowParts&) {
    return {};
}

bool on_main_thread() {
    return SDL_IsMainThread();
}

bool in_move_size_loop() {
    return false;
}

void detach_ime(SDL_Window*) {}

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

std::optional<std::filesystem::path> caption_icon_font() {
    return std::nullopt;
}

KeyModifiers word_modifier() noexcept {
    return KeyModifiers::control;
}

} // namespace elg::detail::platform

namespace elg {

Platform current_platform() noexcept {
    return Platform::mikros;
}

KeyModifiers primary_modifier() noexcept {
    return KeyModifiers::control;
}

} // namespace elg
