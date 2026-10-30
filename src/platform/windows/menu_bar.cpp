// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// The menu bar on Windows (App::menu_bar): a Win32 menu in each window with a system title bar.
// Commands come back as WM_COMMAND (native.cpp's subclass); shortcuts are shown after a tab and
// matched by the app, since SDL's message loop has no accelerator table.

#include "platform/generic/native.h"
#include "platform/windows/shell.h"

#include <windows.h>

#include <algorithm>
#include <map>
#include <string>

namespace elg::detail::platform {

namespace {

native::MenuCommand g_command;
std::vector<menus::Item> g_menus;
std::map<HWND, HMENU> g_windows; // windows with a menu bar (null until there are menus)

std::wstring widen(const std::string& text) {
    std::wstring out(text.size(), L'\0');
    const int n = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(),
                                      static_cast<int>(out.size()));
    out.resize(static_cast<std::size_t>(std::max(n, 0)));
    return out;
}

void add_items(HMENU menu, const std::vector<menus::Item>& items) {
    for (const menus::Item& item : items) {
        if (item.separator) {
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            continue;
        }
        std::wstring title = widen(item.title);
        UINT flags = MF_STRING | (item.enabled ? MF_ENABLED : MF_GRAYED);
        if (!item.submenu.empty()) {
            HMENU submenu = CreatePopupMenu();
            add_items(submenu, item.submenu);
            AppendMenuW(menu, flags | MF_POPUP, reinterpret_cast<UINT_PTR>(submenu), title.c_str());
            continue;
        }
        if (item.shortcut) {
            title += L'\t' + widen(menus::shortcut_text(*item.shortcut));
        }
        if (item.checked && *item.checked) {
            flags |= MF_CHECKED;
        }
        AppendMenuW(menu, flags, item.id, title.c_str());
    }
}

// Gives `window` a menu built from g_menus (or none), keeping its client area's size.
void show(HWND window, HMENU& current) {
    HMENU menu = nullptr;
    if (!g_menus.empty()) {
        menu = CreateMenu();
        add_items(menu, g_menus);
    }
    RECT before{};
    GetClientRect(window, &before);
    SetMenu(window, menu);
    if (current) {
        DestroyMenu(current);
    }
    current = menu;
    RECT after{};
    GetClientRect(window, &after);
    const int grow = (before.bottom - before.top) - (after.bottom - after.top);
    if (grow != 0 && !IsZoomed(window)) {
        RECT frame{};
        GetWindowRect(window, &frame);
        SetWindowPos(window, nullptr, 0, 0, frame.right - frame.left, frame.bottom - frame.top + grow,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

} // namespace

namespace windows {

void attach_menu_bar(HWND window) {
    HMENU& menu = g_windows[window];
    if (!g_menus.empty()) {
        show(window, menu);
    }
}

void detach_menu_bar(HWND window) {
    if (const auto found = g_windows.find(window); found != g_windows.end()) {
        if (found->second) {
            SetMenu(window, nullptr);
            DestroyMenu(found->second);
        }
        g_windows.erase(found);
    }
}

bool menu_command(WORD id) {
    if (id == 0 || !g_command) {
        return false;
    }
    g_command(id);
    return true;
}

} // namespace windows

namespace native {

bool draws_menu_bar_in_content() {
    return false;
}

bool menu_bar_handles_shortcuts() {
    return false;
}

void set_menu_bar(const std::vector<menus::Item>& menus, MenuCommand command) {
    g_menus = menus;
    g_command = std::move(command);
    for (auto& [window, menu] : g_windows) {
        show(window, menu);
    }
}

void set_menu_checked(std::uint32_t id, bool checked) {
    for (const auto& [window, menu] : g_windows) {
        if (menu) {
            CheckMenuItem(menu, id, MF_BYCOMMAND | (checked ? MF_CHECKED : MF_UNCHECKED));
        }
    }
}

} // namespace native

} // namespace elg::detail::platform
