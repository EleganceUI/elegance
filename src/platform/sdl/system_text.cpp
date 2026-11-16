// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include "platform/system_text.h"

#include "core/utf8.h"

#include <string>

namespace elg::detail {

namespace {

// Menu items' titles, submenus included (MenuBarItem, TrayMenuItem).
template <class Item>
void make_valid_titles(std::vector<Item>& items) {
    for (Item& item : items) {
        make_valid_utf8(item.title);
        make_valid_titles(item.submenu);
    }
}

} // namespace

void make_valid_utf8(WindowDesc& desc) {
    make_valid_utf8(desc.title);
}

void make_valid_utf8(std::vector<MenuBarItem>& menus) {
    make_valid_titles(menus);
}

void make_valid_utf8(Notification& notification) {
    make_valid_utf8(notification.title);
    make_valid_utf8(notification.body);
}

void make_valid_utf8(TrayIconOptions& options) {
    make_valid_utf8(options.tooltip);
    make_valid_titles(options.menu);
}

void make_valid_utf8(std::vector<TrayMenuItem>& menu) {
    make_valid_titles(menu);
}

void make_valid_utf8(std::vector<FileFilter>& filters) {
    for (FileFilter& filter : filters) {
        make_valid_utf8(filter.name);
        for (std::string& extension : filter.extensions) {
            make_valid_utf8(extension);
        }
    }
}

} // namespace elg::detail
