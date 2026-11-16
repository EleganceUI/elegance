// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_PLATFORM_GENERIC_SYSTEM_TEXT_H
#define ELG_PLATFORM_GENERIC_SYSTEM_TEXT_H

// The text Elegance hands the system: window titles, the menu bar, notifications, the tray icon,
// file dialogs and the badge. The public entry points (App::window, App::menu_bar, notify(),
// TrayIcon, choose_files(), ...) replace malformed UTF-8 in it with U+FFFD (core/utf8.h) through
// these, so every platform's code gets well-formed strings, as the system APIs require.

#include <elg/app.h>
#include <elg/desktop.h>

#include <vector>

namespace elg::detail {

void make_valid_utf8(WindowDesc& desc);
void make_valid_utf8(std::vector<MenuBarItem>& menus); // and their submenus
void make_valid_utf8(Notification& notification);
void make_valid_utf8(TrayIconOptions& options);
void make_valid_utf8(std::vector<TrayMenuItem>& menu); // and their submenus
void make_valid_utf8(std::vector<FileFilter>& filters);

} // namespace elg::detail

#endif // ELG_PLATFORM_GENERIC_SYSTEM_TEXT_H
