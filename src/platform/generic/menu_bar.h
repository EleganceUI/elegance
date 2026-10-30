// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_PLATFORM_GENERIC_MENU_BAR_H
#define ELG_PLATFORM_GENERIC_MENU_BAR_H

// The menu bar (App::menu_bar) as this platform shows it: roles resolved to titles and
// shortcuts, items the platform has no use for left out. Each OS shows the result its way
// (platform/<os>/menu_bar.*); Linux draws it in the window with menu_bar_view().

#include <elg/app.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace elg::detail::menus {

struct Item {
    // Its position in a pre-order walk of the menus App::menu_bar was given (from 1), so a
    // command finds the item it came from. 0 for separators and menus added here.
    std::uint32_t id = 0;
    std::string title; // empty for roles whose title only the platform knows (macOS's app name)
    bool separator = false;
    bool enabled = true;
    std::optional<bool> checked;
    std::optional<KeyboardShortcut> shortcut;
    MenuRole role = MenuRole::none;
    // Whether choosing it runs a command (App's); false for roles the platform carries out itself.
    bool has_action = true;
    std::vector<Item> submenu;
    bool operator==(const Item&) const = default;
};

// The menus for this platform. `macos` keeps macOS's roles (the app, hide and full-screen
// items); elsewhere they're left out and the app menu's items join the first menu.
std::vector<Item> resolve(const std::vector<MenuBarItem>& menus, bool macos);

// The item with pre-order position `id`, or nullptr.
const MenuBarItem* find(const std::vector<MenuBarItem>& menus, std::uint32_t id);

// The role's standard shortcut on this platform (nullopt: none, or one the platform sets itself,
// like macOS's Cmd+,).
std::optional<KeyboardShortcut> standard_shortcut(MenuRole role);

// The key a role sends to the focused view (undo, cut, copy, ...), or nullopt.
std::optional<KeyboardShortcut> edit_key(MenuRole role);

// "Ctrl+Shift+S", as Windows and Linux menus show shortcuts.
std::string shortcut_text(const KeyboardShortcut& shortcut);

// The id of the enabled item whose shortcut is `shortcut`, or 0.
std::uint32_t match_shortcut(const std::vector<Item>& menus, const KeyboardShortcut& shortcut);

// Linux: the bar of menus at the top of a window. `menus` is read reactively (it changes when
// App::menu_bar is called again); `command` runs an item; `checked` says whether a check box is.
View menu_bar_view(Prop<std::vector<Item>> menus, std::function<void(std::uint32_t)> command,
                   std::function<bool(std::uint32_t)> checked);

} // namespace elg::detail::menus

#endif // ELG_PLATFORM_GENERIC_MENU_BAR_H
