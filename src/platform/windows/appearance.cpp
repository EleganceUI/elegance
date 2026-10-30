// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Appearance on Windows (platform/generic/native.h): window materials as DWM system backdrops
// (Windows 11 22H2 and later), drawn behind content extended over the whole window, with the
// title bar's light/dark following the system so the tint matches; and the accent color from
// Settings > Personalization > Colors.

#include "platform/generic/native.h"
#include "platform/windows/shell.h"

#include <SDL3/SDL.h>

#include <windows.h>
#include <dwmapi.h>

#include <map>

namespace elg::detail::platform {

namespace {

constexpr DWORD kUseImmersiveDarkMode = 20; // DWMWA_USE_IMMERSIVE_DARK_MODE
constexpr DWORD kSystemBackdropType = 38;   // DWMWA_SYSTEMBACKDROP_TYPE

// Windows' build number (GetVersionEx lies to apps without a manifest; RtlGetVersion doesn't).
DWORD windows_build() {
    static const DWORD build = [] {
        using RtlGetVersion = LONG(WINAPI*)(OSVERSIONINFOW*);
        OSVERSIONINFOW info{sizeof(info)};
        if (HMODULE ntdll = GetModuleHandleW(L"ntdll.dll")) {
            if (auto get = reinterpret_cast<RtlGetVersion>(GetProcAddress(ntdll, "RtlGetVersion"))) {
                get(&info);
            }
        }
        return info.dwBuildNumber;
    }();
    return build;
}

// DWM_SYSTEMBACKDROP_TYPE.
DWORD backdrop_of(WindowMaterial material) {
    switch (material) {
    case WindowMaterial::sidebar: return 4; // DWMSBT_TABBEDWINDOW: Mica Alt
    case WindowMaterial::acrylic: return 3; // DWMSBT_TRANSIENTWINDOW
    default: return 2;                      // DWMSBT_MAINWINDOW: Mica
    }
}

std::map<HWND, WindowMaterial> g_materials; // UI thread

void set_dark_title_bar(HWND window) {
    const BOOL dark = SDL_GetSystemTheme() == SDL_SYSTEM_THEME_DARK;
    DwmSetWindowAttribute(window, kUseImmersiveDarkMode, &dark, sizeof(dark));
}

} // namespace

namespace windows {

void apply_material(HWND window, WindowMaterial material) {
    if (!native::supports_material(material)) {
        return;
    }
    g_materials[window] = material;
    // The backdrop shows where the frame is, so the frame covers the client area.
    const MARGINS everywhere{-1, -1, -1, -1};
    DwmExtendFrameIntoClientArea(window, &everywhere);
    const DWORD backdrop = backdrop_of(material);
    DwmSetWindowAttribute(window, kSystemBackdropType, &backdrop, sizeof(backdrop));
    set_dark_title_bar(window);
}

void refresh_material(HWND window) {
    if (g_materials.contains(window)) {
        set_dark_title_bar(window);
    }
}

void forget_material(HWND window) {
    g_materials.erase(window);
}

void accent_changed() {
    // The app re-reads the accent color (and the light/dark setting) on this event.
    SDL_Event event{};
    event.type = SDL_EVENT_SYSTEM_THEME_CHANGED;
    SDL_PushEvent(&event);
}

} // namespace windows

namespace native {

bool supports_material(WindowMaterial material) {
    return material != WindowMaterial::none && windows_build() >= 22621;
}

std::optional<Color> system_accent_color() {
    // 0xAABBGGRR, as the Settings app stores it.
    DWORD value = 0;
    DWORD size = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\DWM", L"AccentColor", RRF_RT_REG_DWORD,
                     nullptr, &value, &size) != ERROR_SUCCESS) {
        return std::nullopt;
    }
    return Color::rgb(static_cast<std::uint8_t>(value), static_cast<std::uint8_t>(value >> 8),
                      static_cast<std::uint8_t>(value >> 16));
}

void watch_appearance() {
    // Each window's subclass hears the changes (native.cpp).
}

void unwatch_appearance() {}

} // namespace native

} // namespace elg::detail::platform
