// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_PLATFORM_SDL_NATIVE_H
#define ELG_PLATFORM_SDL_NATIVE_H

// What each OS does differently under SDL. platform_sdl.cpp is the same everywhere and calls
// these; every OS implements all of them (platform/windows/native.cpp, platform/macos/native.mm,
// platform/linux/native.cpp), mostly as nothing. Those the app calls itself are in the platform
// interface (platform/system.h).

#include "platform/menu_bar.h"
#include "platform/platform.h"
#include "platform/system.h"

#include <elg/desktop.h>
#include <elg/theme.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

struct SDL_Window;

namespace elg::detail::platform::native {

// The OS's parts of a window.
struct WindowParts {
    void* metal_view = nullptr; // SDL_MetalView on macOS: the window's CAMetalLayer
    void* title_bar = nullptr;  // TitleBar::integrated on macOS (platform/macos/title_bar.h)
};

// SDL window flags the GPU surface needs (macOS: SDL_WINDOW_METAL).
std::uint64_t gpu_window_flags();

// Right after `window` is created (still hidden): the OS's own parts, the material, the menu
// bar. Throws PlatformError, having destroyed `window`, if it can't be drawn into.
WindowParts window_created(SDL_Window* window, const WindowDesc& desc, bool popup);
// Before `window` is destroyed.
void window_destroying(SDL_Window* window, WindowParts& parts);

SurfaceSource surface_source(SDL_Window* window, const WindowParts& parts);
void* native_handle(SDL_Window* window);
// Where the system draws its window controls (logical points), or empty.
Rect system_controls_frame(WindowParts& parts);

// Whether the OS is running its modal move/size loop on this thread (the user is dragging a
// window edge), during which our event loop doesn't run.
bool in_move_size_loop();

// Secure text input: detaches the IME from `window` (where SDL doesn't do it for the password
// input type), and switches system-wide Secure Event Input on or off (macOS).
void detach_ime(SDL_Window* window);
void set_secure_event_input(bool on);

// Input SDL doesn't deliver (macOS trackpad phases, momentum and gestures). `push` gets each as
// an Event with window coordinates (wheel deltas: points if precise, else notches; modifiers are
// filled in by the caller) and returns whether it took it. Returns a handle, or nullptr.
using InputSink = std::function<bool(Event)>;
void* install_input_monitor(const InputSink& push);
void remove_input_monitor(void* monitor);

// Shows a system notification (platform/<os>/notifications.*). UI thread, while an App runs.
void show_notification(const Notification& notification);

// --- Dock and taskbar (platform/<os>/dock.*). UI thread, while an App runs. ---

void set_badge(const std::optional<std::string>& text);
void set_progress(std::optional<double> fraction, ProgressState state);

// --- Appearance (platform/<os>/appearance.*) ---

// Whether this system draws `material` behind windows; window_created applies it (the window is
// then made transparent).
bool supports_material(WindowMaterial material);
// The system's light/dark setting changed (Windows: Mica's tint follows the title bar's).
void appearance_changed(SDL_Window* window, WindowParts& parts);
// Between Platform's construction and destruction: changes to the accent color arrive as
// SDL_EVENT_SYSTEM_THEME_CHANGED, where SDL doesn't send one itself.
void watch_appearance();
void unwatch_appearance();

// Sets the UI thread up for a run, before SDL starts (Windows: OLE, in a single-threaded
// apartment, ELGSPEC-0019.R1): true if end_run() has something to undo. A failure is logged.
bool begin_run();
// Undoes begin_run(), after SDL quits, and lets go of what the run made in its apartment.
void end_run(bool begun) noexcept;

} // namespace elg::detail::platform::native

#endif // ELG_PLATFORM_SDL_NATIVE_H
