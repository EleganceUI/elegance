// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include "view/menu_bar_view.h"

#include <elg/styles.h>
#include <elg/text.h>
#include <elg/views.h>

#include <cstddef>
#include <utility>

namespace elg::detail::menus {

namespace {

View menu_entries(const std::vector<Item>& items, const Command& command,
                  const std::function<bool(std::uint32_t)>& checked) {
    std::vector<View> views;
    for (const Item& item : items) {
        if (item.separator) {
            views.push_back(Separator());
            continue;
        }
        View view;
        if (!item.submenu.empty()) {
            view = Submenu(item.title, [submenu = item.submenu, command, checked] {
                return menu_entries(submenu, command, checked);
            });
        } else {
            MenuItemOptions options;
            if (item.shortcut) {
                options.shortcut = shortcut_text(*item.shortcut);
            }
            if (item.checked) {
                options.checked = Prop<bool>([checked, id = item.id] { return checked(id); });
            }
            view = MenuItem(item.title, [command, source = item.source, id = item.id] { command(source, id); },
                            std::move(options));
        }
        views.push_back(item.enabled ? std::move(view) : view.disabled());
    }
    return detail::make_menu(std::move(views));
}

} // namespace

View menu_bar_view(const Prop<std::vector<Item>>& menus, const Command& command,
                   const std::function<bool(std::uint32_t)>& checked) {
    const auto indexed = [menus] {
        std::vector<std::pair<std::size_t, Item>> result;
        const std::vector<Item> items = menus.get();
        result.reserve(items.size());
        for (std::size_t i = 0; i < items.size(); ++i) {
            result.emplace_back(i, items[i]);
        }
        return result;
    };
    return HStack({.spacing = 0},
                  ForEach(indexed, [](const std::pair<std::size_t, Item>& entry) { return entry.first; },
                          [command, checked](const std::pair<std::size_t, Item>& entry) {
                              const Item& menu = entry.second;
                              return MenuButton(Text(menu.title).font(Font::callout).padding({.top = 4, .leading = 10, .bottom = 4, .trailing = 10}),
                                                [submenu = menu.submenu, command, checked] {
                                                    return menu_entries(submenu, command, checked);
                                                },
                                                {.shows_indicator = false})
                                  .button_style(PlainButtonStyle{})
                                  .disabled(!menu.enabled);
                          }),
                  Spacer())
        .frame({.max_width = infinity})
        .background(ThemeColor::surface);
}

} // namespace elg::detail::menus
