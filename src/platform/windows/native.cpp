// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Windows under SDL (platform/generic/native.h): WebGPU draws into the HWND, DWM rounds
// undecorated windows' corners, and password fields detach the IME.

#include "platform/generic/native.h"

#include <SDL3/SDL.h>

#include <windows.h>
#include <dwmapi.h>
#include <imm.h>

namespace elg::detail::platform {

namespace native {

namespace {

HWND hwnd_of(SDL_Window* window) {
    return static_cast<HWND>(
        SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
}

} // namespace

std::uint64_t gpu_window_flags() {
    return 0;
}

WindowParts window_created(SDL_Window* window, const WindowDesc& desc, bool) {
    // Windows 11 rounds the corners of decorated windows; ask the same for undecorated opaque
    // ones (transparent windows draw their own shape). Ignored on Windows 10.
    if (desc.title_bar == TitleBar::hidden && !desc.transparent) {
        if (HWND hwnd = hwnd_of(window)) {
            const DWORD round = 2; // DWMWCP_ROUND
            DwmSetWindowAttribute(hwnd, 33 /* DWMWA_WINDOW_CORNER_PREFERENCE */, &round, sizeof(round));
        }
    }
    return {};
}

void window_destroying(SDL_Window*, WindowParts&) {}

SurfaceSource surface_source(SDL_Window* window, const WindowParts&) {
    const SDL_PropertiesID properties = SDL_GetWindowProperties(window);
    SurfaceSource source;
    source.kind = SurfaceSource::Kind::win32;
    source.display = SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_WIN32_INSTANCE_POINTER, nullptr);
    source.handle = SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
    return source;
}

void* native_handle(SDL_Window* window) {
    return hwnd_of(window);
}

Rect system_controls_frame(WindowParts&) {
    return {};
}

bool in_move_size_loop() {
    GUITHREADINFO info{sizeof(GUITHREADINFO)};
    return GetGUIThreadInfo(GetCurrentThreadId(), &info) && (info.flags & GUI_INMOVESIZE) != 0;
}

void detach_ime(SDL_Window* window) {
    // SDL ignores the password type here; a password edit control has no IME. SDL associates its
    // context again the next time text input starts.
    if (HWND hwnd = hwnd_of(window)) {
        ImmAssociateContext(hwnd, nullptr);
    }
}

void set_secure_event_input(bool) {}

void* install_input_monitor(InputSink) {
    return nullptr;
}

void remove_input_monitor(void*) {}

} // namespace native

void wait_for_compositor() {
    DwmFlush();
}

bool debugger_present() noexcept {
    return IsDebuggerPresent() != 0;
}

void debug_break() noexcept {
    __debugbreak();
}

} // namespace elg::detail::platform
