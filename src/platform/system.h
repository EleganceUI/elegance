// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_PLATFORM_SYSTEM_H
#define ELG_PLATFORM_SYSTEM_H

// What the app asks the system directly, as part of the platform interface: the main thread, the
// person's accent color, the notifications' handlers and the system's menu bar. Every OS
// implements these with the rest of what it does differently (platform/sdl/native.h).

#include "platform/menu_bar.h"

#include <elg/color.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

namespace elg::detail::platform::native {

// Whether this is the process's main thread, the one that ran main().
bool on_main_thread();

// The person's accent color, or nullopt. UI thread.
std::optional<Color> system_accent_color();

// Destroys the click handlers of the notifications shown so far: a click on them after this does
// nothing. UI thread, as an App's run ends.
void forget_notification_handlers();

// --- Menu bar (platform/<os>/menu_bar.*) ---

// Whether the menu bar is drawn inside each window (menus::menu_bar_view), rather than by the
// system.
bool draws_menu_bar_in_content();
// Whether the system's menus take their own shortcuts (macOS's key equivalents); elsewhere the
// app matches them after the views turn the key down.
bool menu_bar_handles_shortcuts();
// Shows `menus` (replacing any before): macOS's menu bar, or every Windows window's with a system
// title bar, now and when created. `command` gets the id of a chosen item that has an action.
// UI thread.
using MenuCommand = std::function<void(std::uint32_t id)>;
void set_menu_bar(const std::vector<menus::Item>& menus, const MenuCommand& command);
// Drops set_menu_bar()'s `command`: items chosen after this do nothing. UI thread, as an App's run
// ends.
void forget_menu_command();
// A check box item's mark.
void set_menu_checked(std::uint32_t id, bool checked);

} // namespace elg::detail::platform::native

#endif // ELG_PLATFORM_SYSTEM_H
