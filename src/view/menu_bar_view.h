// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_VIEW_MENU_BAR_VIEW_H
#define ELG_VIEW_MENU_BAR_VIEW_H

// The menu bar drawn in the window, where the system doesn't draw one (Linux): the platform's
// resolved menus (platform/menu_bar.h) as views.

#include "platform/menu_bar.h"

#include <elg/prop.h>
#include <elg/view.h>

#include <cstdint>
#include <functional>
#include <vector>

namespace elg::detail::menus {

// The bar of menus at the top of a window. `menus` is read reactively (it changes when
// App::menu_bar is called again); `command` runs an item; `checked` says whether a check box is.
View menu_bar_view(const Prop<std::vector<Item>>& menus, const Command& command,
                   const std::function<bool(std::uint32_t)>& checked);

} // namespace elg::detail::menus

#endif // ELG_VIEW_MENU_BAR_VIEW_H
