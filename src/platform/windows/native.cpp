// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Windows under SDL (platform/sdl/native.h): WebGPU draws into the HWND, DWM rounds
// undecorated windows' corners, and password fields detach the IME. Top-level windows are
// subclassed for what SDL doesn't pass on: menu commands, the taskbar button's creation, and
// accent color changes.

#include "platform/sdl/native.h"
#include "platform/windows/shell.h"

#include "core/log.h"
#include "core/ui_tasks.h"

#include <elg/events.h>
#include <elg/platform.h>

#include <SDL3/SDL.h>

#include <windows.h>
#include <ole2.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <imm.h>

#include <cstdint>
#include <filesystem>
#include <system_error>

namespace elg::detail::platform {

namespace native {

namespace {

// Recorded while the program loads: static initialization runs on the main thread, also when
// Elegance is a DLL that the program links.
const DWORD g_main_thread = GetCurrentThreadId();

} // namespace

bool on_main_thread() {
    return GetCurrentThreadId() == g_main_thread;
}

namespace {

HWND hwnd_of(SDL_Window* window) {
    return static_cast<HWND>(
        SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
}

constexpr UINT_PTR kSubclassId = 0x454C47; // "ELG"

// Windows calls it: nothing may escape it (ELGSPEC-0002.R10).
LRESULT CALLBACK subclass_procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR, DWORD_PTR) {
    static const UINT taskbar_button_created = RegisterWindowMessageW(L"TaskbarButtonCreated");
    bool handled = false;
    run_foreign_callback([&] {
        if (message == WM_COMMAND && HIWORD(wparam) == 0 && lparam == 0 && windows::menu_command(LOWORD(wparam))) {
            handled = true;
            return;
        }
        if (message == taskbar_button_created) {
            windows::taskbar_button_created(window);
        } else if (message == WM_DWMCOLORIZATIONCOLORCHANGED ||
                   (message == WM_SETTINGCHANGE && lparam &&
                    lstrcmpW(reinterpret_cast<const wchar_t*>(lparam), L"ImmersiveColorSet") == 0)) {
            windows::accent_changed();
        }
    });
    return handled ? 0 : DefSubclassProc(window, message, wparam, lparam);
}

} // namespace

std::uint64_t gpu_window_flags() {
    return 0;
}

WindowParts window_created(SDL_Window* window, const WindowDesc& desc, bool popup) {
    HWND hwnd = hwnd_of(window);
    if (hwnd && !popup) {
        SetWindowSubclass(hwnd, &subclass_procedure, kSubclassId, 0);
        windows::apply_material(hwnd, desc.material);
        if (desc.title_bar != TitleBar::hidden) {
            windows::attach_menu_bar(hwnd);
        }
    }
    // Windows 11 rounds the corners of decorated windows; ask the same for undecorated opaque
    // ones (transparent windows draw their own shape). Ignored on Windows 10.
    if (hwnd && desc.title_bar == TitleBar::hidden && !desc.transparent) {
        const DWORD round = 2; // DWMWCP_ROUND
        DwmSetWindowAttribute(hwnd, 33 /* DWMWA_WINDOW_CORNER_PREFERENCE */, &round, sizeof(round));
    }
    return {};
}

void window_destroying(SDL_Window* window, WindowParts&) {
    if (HWND hwnd = hwnd_of(window)) {
        windows::detach_menu_bar(hwnd);
        windows::forget_material(hwnd);
        RemoveWindowSubclass(hwnd, &subclass_procedure, kSubclassId);
    }
}

void appearance_changed(SDL_Window* window, WindowParts&) {
    if (HWND hwnd = hwnd_of(window)) {
        windows::refresh_material(hwnd);
    }
}

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

void require_tray_icons() {}

void set_secure_event_input(bool) {}

void* install_input_monitor(const InputSink&) {
    return nullptr;
}

void remove_input_monitor(void*) {}

// SDL's drag and drop, the clipboard and the shell's dialogs need OLE on the UI thread
// (ELGSPEC-0019.R2). Elegance sets COM up on no other thread it didn't start, so only the app can
// have put this one in another apartment.
bool begin_run() {
    const HRESULT result = OleInitialize(nullptr);
    if (FAILED(result)) {
        log_warning("App::run: OLE can't be set up on this thread (HRESULT {:#010x}), so drag and drop, the "
                    "clipboard and file dialogs may not work: did the app set COM up on it in the "
                    "multithreaded apartment?",
                    static_cast<std::uint32_t>(result));
        return false;
    }
    return true;
}

void end_run(bool begun) noexcept {
    windows::release_taskbar(); // made in this apartment
    if (begun) {
        OleUninitialize();
    }
}

} // namespace native

void wait_for_compositor() {
    DwmFlush();
}

bool presents_unchanged_exposes() {
    return false;
}

std::optional<std::filesystem::path> caption_icon_font() {
    wchar_t windows[MAX_PATH] = L"C:\\Windows";
    GetWindowsDirectoryW(windows, MAX_PATH);
    std::filesystem::path file = std::filesystem::path(windows) / "Fonts" / "segmdl2.ttf";
    std::error_code error;
    if (std::filesystem::exists(file, error)) {
        return file;
    }
    return std::nullopt;
}

KeyModifiers word_modifier() noexcept {
    return KeyModifiers::control;
}

} // namespace elg::detail::platform

namespace elg {

Platform current_platform() noexcept {
    return Platform::windows;
}

KeyModifiers primary_modifier() noexcept {
    return KeyModifiers::control;
}

} // namespace elg
