// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// The menu bar on Linux (App::menu_bar): there's no global menu to rely on, so the app draws it
// at the top of each window's content (menu_bar_view) and matches its shortcuts itself.

#include "platform/generic/native.h"

namespace elg::detail::platform::native {

bool draws_menu_bar_in_content() {
    return true;
}

bool menu_bar_handles_shortcuts() {
    return false;
}

void set_menu_bar(const std::vector<menus::Item>&, MenuCommand) {}

void set_menu_checked(std::uint32_t, bool) {}

} // namespace elg::detail::platform::native
