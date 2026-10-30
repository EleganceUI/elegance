// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// The taskbar on Windows (elg/desktop.h): progress on every window's taskbar button
// (ITaskbarList3), and the badge as an overlay icon drawn here: a red disc with the text. Buttons
// made later (TaskbarButtonCreated) get the current state.

#include "platform/generic/native.h"
#include "platform/windows/shell.h"

#include "core/log.h"

#include <SDL3/SDL.h>

#include <windows.h>
#include <shobjidl.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <vector>

namespace elg::detail::platform {

namespace {

using Microsoft::WRL::ComPtr;

std::optional<std::string> g_badge;
std::optional<double> g_fraction;
ProgressState g_state = ProgressState::normal;

ITaskbarList3* taskbar() {
    static ComPtr<ITaskbarList3> list = [] {
        const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        (void)com; // already initialized (SDL's OLE drag and drop): fine either way
        ComPtr<ITaskbarList3> created;
        if (FAILED(CoCreateInstance(CLSID_TaskbarList, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&created))) ||
            FAILED(created->HrInit())) {
            log_warning("taskbar: not available");
            return ComPtr<ITaskbarList3>();
        }
        return created;
    }();
    return list.Get();
}

// The app's top-level windows (not menus or tooltips).
std::vector<HWND> app_windows() {
    std::vector<HWND> result;
    int count = 0;
    SDL_Window** windows = SDL_GetWindows(&count);
    for (int i = 0; windows && i < count; ++i) {
        if ((SDL_GetWindowFlags(windows[i]) & (SDL_WINDOW_POPUP_MENU | SDL_WINDOW_TOOLTIP)) != 0) {
            continue;
        }
        if (auto* hwnd = static_cast<HWND>(SDL_GetPointerProperty(SDL_GetWindowProperties(windows[i]),
                                                                  SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr))) {
            result.push_back(hwnd);
        }
    }
    SDL_free(windows);
    return result;
}

std::wstring widen(const std::string& text) {
    std::wstring out(text.size(), L'\0');
    const int n = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(),
                                      static_cast<int>(out.size()));
    out.resize(static_cast<std::size_t>(std::max(n, 0)));
    return out;
}

// A red disc with `text` in white, as an icon for the taskbar's overlay (shown at 16x16 points).
HICON badge_icon(const std::wstring& text, int size) {
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = size;
    info.bmiHeader.biHeight = -size; // top down
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    HDC screen = GetDC(nullptr);
    HDC dc = CreateCompatibleDC(screen);
    ReleaseDC(nullptr, screen);

    // The text alone, white on black: its coverage.
    void* text_bits = nullptr;
    HBITMAP text_bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &text_bits, nullptr, 0);
    if (!text_bitmap) {
        DeleteDC(dc);
        return nullptr;
    }
    HGDIOBJ old_bitmap = SelectObject(dc, text_bitmap);
    std::fill_n(static_cast<std::uint32_t*>(text_bits), size * size, 0u);
    const int height = text.size() <= 1 ? size * 3 / 4 : text.size() == 2 ? size * 5 / 8 : size / 2;
    HFONT font = CreateFontW(-height, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                             CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    HGDIOBJ old_font = SelectObject(dc, font);
    SetTextColor(dc, RGB(255, 255, 255));
    SetBkMode(dc, TRANSPARENT);
    RECT area{0, 0, size, size};
    DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &area, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
    GdiFlush();
    SelectObject(dc, old_font);
    DeleteObject(font);
    SelectObject(dc, old_bitmap);

    // The icon: the disc (edges smoothed by distance), white where the text covers it.
    void* bits = nullptr;
    HBITMAP color = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    DeleteDC(dc);
    if (!color) {
        DeleteObject(text_bitmap);
        return nullptr;
    }
    const auto* coverage = static_cast<const std::uint32_t*>(text_bits);
    auto* out = static_cast<std::uint32_t*>(bits);
    const float center = size / 2.f;
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const float distance = std::hypot(x + 0.5f - center, y + 0.5f - center);
            const float disc = std::clamp(center - distance, 0.f, 1.f);
            const float ink = static_cast<float>(coverage[y * size + x] & 0xFF) / 255.f;
            const auto channel = [&](float base) {
                return static_cast<std::uint32_t>(std::lround((base + (255 - base) * ink)));
            };
            // 0xE81123: Windows' red. Straight alpha.
            out[y * size + x] = (static_cast<std::uint32_t>(std::lround(disc * 255)) << 24) | (channel(0xE8) << 16) |
                                (channel(0x11) << 8) | channel(0x23);
        }
    }
    DeleteObject(text_bitmap);
    HBITMAP mask = CreateBitmap(size, size, 1, 1, nullptr);
    ICONINFO icon{TRUE, 0, 0, mask, color};
    HICON result = CreateIconIndirect(&icon);
    DeleteObject(mask);
    DeleteObject(color);
    return result;
}

void show_progress(ITaskbarList3* list, HWND window) {
    if (!g_fraction) {
        list->SetProgressState(window, TBPF_NOPROGRESS);
        return;
    }
    switch (g_state) {
    case ProgressState::indeterminate: list->SetProgressState(window, TBPF_INDETERMINATE); return;
    case ProgressState::paused: list->SetProgressState(window, TBPF_PAUSED); break;
    case ProgressState::error: list->SetProgressState(window, TBPF_ERROR); break;
    case ProgressState::normal: list->SetProgressState(window, TBPF_NORMAL); break;
    }
    list->SetProgressValue(window, static_cast<ULONGLONG>(std::lround(*g_fraction * 1000)), 1000);
}

void show_badge(ITaskbarList3* list, HWND window) {
    if (!g_badge || g_badge->empty()) {
        list->SetOverlayIcon(window, nullptr, nullptr);
        return;
    }
    const std::wstring text = widen(*g_badge);
    const int size = std::max(16, GetSystemMetrics(SM_CXSMICON) * 2);
    if (HICON icon = badge_icon(text, size)) {
        list->SetOverlayIcon(window, icon, text.c_str()); // the taskbar keeps a copy
        DestroyIcon(icon);
    }
}

} // namespace

namespace windows {

void taskbar_button_created(HWND window) {
    if (ITaskbarList3* list = taskbar()) {
        show_progress(list, window);
        show_badge(list, window);
    }
}

} // namespace windows

namespace native {

void set_badge(const std::optional<std::string>& text) {
    g_badge = text;
    if (ITaskbarList3* list = taskbar()) {
        for (HWND window : app_windows()) {
            show_badge(list, window);
        }
    }
}

void set_progress(std::optional<double> fraction, ProgressState state) {
    g_fraction = fraction;
    g_state = state;
    if (ITaskbarList3* list = taskbar()) {
        for (HWND window : app_windows()) {
            show_progress(list, window);
        }
    }
}

} // namespace native

} // namespace elg::detail::platform
