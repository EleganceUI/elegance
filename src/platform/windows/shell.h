// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_PLATFORM_WINDOWS_SHELL_H
#define ELG_PLATFORM_WINDOWS_SHELL_H

// What native.cpp does to each top-level window on Windows, from the other files here.

#include <elg/app.h>

#include <windows.h>

namespace elg::detail::platform::windows {

// appearance.cpp: the window's material (DWM system backdrop), if it has one; and its title
// bar's light/dark, which Mica's tint follows.
void apply_material(HWND window, WindowMaterial material);
void refresh_material(HWND window);
void forget_material(HWND window);
// The person changed the accent color (WM_SETTINGCHANGE "ImmersiveColorSet",
// WM_DWMCOLORIZATIONCOLORCHANGED).
void accent_changed();

// menu_bar.cpp: windows with a system title bar get the menu bar.
void attach_menu_bar(HWND window);
void detach_menu_bar(HWND window);
// WM_COMMAND from a menu: true if it was one of ours.
bool menu_command(WORD id);

// dock.cpp: the taskbar button shows the progress and badge set before it existed.
void taskbar_button_created(HWND window);

} // namespace elg::detail::platform::windows

#endif // ELG_PLATFORM_WINDOWS_SHELL_H
