// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// macOS under SDL (platform/generic/native.h): windows present through a CAMetalLayer, integrated
// title bars and trackpad input come from AppKit (title_bar.mm, input.mm).

#include "platform/generic/native.h"

#include "platform/macos/input.h"
#include "platform/macos/title_bar.h"

#include <elg/error.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_metal.h>

#include <sys/sysctl.h>
#include <unistd.h>

#include <csignal>
#include <format>

namespace elg::detail::platform {

namespace native {

std::uint64_t gpu_window_flags() {
    return SDL_WINDOW_METAL;
}

WindowParts window_created(SDL_Window* window, const WindowDesc& desc, bool popup) {
    WindowParts parts;
    // Views belong to the main thread, so the layer is made here rather than by the renderer.
    parts.metal_view = SDL_Metal_CreateView(window);
    if (!parts.metal_view) {
        const std::string error = SDL_GetError();
        SDL_DestroyWindow(window);
        throw PlatformError(std::format("failed to create a Metal view: {}", error));
    }
    if (desc.title_bar == TitleBar::integrated && !popup) {
        parts.title_bar = macos::integrate_title_bar(
            SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr),
            desc.system_controls_position);
    }
    return parts;
}

void window_destroying(SDL_Window*, WindowParts& parts) {
    if (parts.title_bar) {
        macos::release_title_bar(parts.title_bar);
    }
    if (parts.metal_view) {
        SDL_Metal_DestroyView(static_cast<SDL_MetalView>(parts.metal_view));
    }
}

SurfaceSource surface_source(SDL_Window*, const WindowParts& parts) {
    SurfaceSource source;
    source.kind = SurfaceSource::Kind::metal;
    source.handle = SDL_Metal_GetLayer(static_cast<SDL_MetalView>(parts.metal_view));
    return source;
}

void* native_handle(SDL_Window*) {
    return nullptr;
}

Rect system_controls_frame(WindowParts& parts) {
    if (!parts.title_bar) {
        return {};
    }
    macos::refresh_title_bar(parts.title_bar);
    return macos::system_controls_frame(parts.title_bar); // points are logical points on macOS
}

bool in_move_size_loop() {
    return false;
}

void detach_ime(SDL_Window*) {
    // Secure Event Input turns the IME off.
}

void set_secure_event_input(bool on) {
    macos::set_secure_event_input(on);
}

void* install_input_monitor(InputSink push) {
    return macos::install_input_monitor([push = std::move(push)](const macos::NativeInput& input) {
        SDL_Window* window = nullptr;
        int count = 0;
        if (SDL_Window** windows = SDL_GetWindows(&count)) {
            for (int i = 0; i < count && !window; ++i) {
                if (SDL_GetPointerProperty(SDL_GetWindowProperties(windows[i]), SDL_PROP_WINDOW_COCOA_WINDOW_POINTER,
                                           nullptr) == input.ns_window) {
                    window = windows[i];
                }
            }
            SDL_free(windows);
        }
        if (!window) {
            return false;
        }
        Event event;
        event.window_id = SDL_GetWindowID(window);
        event.x = input.x; // SDL's window coordinates are points on macOS
        event.y = input.y;
        event.phase = input.phase;
        event.amount = input.amount;
        event.time = input.time;
        switch (input.kind) {
        case macos::NativeInput::Kind::scroll:
            event.type = EventType::wheel;
            event.precise = input.precise;
            event.momentum = input.momentum;
            event.scroll_dx = input.dx;
            event.scroll_dy = input.dy;
            break;
        case macos::NativeInput::Kind::magnify: event.type = EventType::magnify; break;
        case macos::NativeInput::Kind::rotate: event.type = EventType::rotate; break;
        case macos::NativeInput::Kind::two_finger_double_tap: event.type = EventType::two_finger_double_tap; break;
        }
        return push(std::move(event));
    });
}

void remove_input_monitor(void* monitor) {
    macos::remove_input_monitor(monitor);
}

} // namespace native

void wait_for_compositor() {}

bool debugger_present() noexcept {
    int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PID, getpid()};
    kinfo_proc info{};
    std::size_t size = sizeof(info);
    if (sysctl(mib, 4, &info, &size, nullptr, 0) != 0) {
        return false;
    }
    return (info.kp_proc.p_flag & P_TRACED) != 0;
}

void debug_break() noexcept {
    std::raise(SIGTRAP);
}

} // namespace elg::detail::platform
