// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include "platform/generic/menu_bar.h"

#include <elg/platform.h>
#include <elg/styles.h>
#include <elg/text.h>
#include <elg/views.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace elg::detail::menus {

namespace {

bool macos_only(MenuRole role) {
    switch (role) {
    case MenuRole::hide:
    case MenuRole::hide_others:
    case MenuRole::show_all:
    case MenuRole::toggle_fullscreen: return true;
    default: return false;
    }
}

// macOS carries these out itself when the item has no action of its own.
bool native_on_macos(MenuRole role) {
    switch (role) {
    case MenuRole::about:
    case MenuRole::hide:
    case MenuRole::hide_others:
    case MenuRole::show_all:
    case MenuRole::minimize:
    case MenuRole::zoom:
    case MenuRole::toggle_fullscreen: return true;
    default: return false;
    }
}

// The standard title, or empty where macOS fills in the app's name.
std::string standard_title(MenuRole role, bool macos) {
    switch (role) {
    case MenuRole::about: return macos ? "" : "About";
    case MenuRole::settings: return macos ? "Settings…" : "Settings";
    case MenuRole::quit: return macos ? "" : current_platform() == Platform::windows ? "Exit" : "Quit";
    case MenuRole::hide: return "";
    case MenuRole::hide_others: return "Hide Others";
    case MenuRole::show_all: return "Show All";
    case MenuRole::undo: return "Undo";
    case MenuRole::redo: return "Redo";
    case MenuRole::cut: return "Cut";
    case MenuRole::copy: return "Copy";
    case MenuRole::paste: return "Paste";
    case MenuRole::select_all: return "Select All";
    case MenuRole::minimize: return "Minimize";
    case MenuRole::zoom: return macos ? "Zoom" : "Maximize";
    case MenuRole::toggle_fullscreen: return "Enter Full Screen";
    case MenuRole::window: return "Window";
    case MenuRole::help: return "Help";
    default: return "";
    }
}

bool keeps(const MenuBarItem& item, bool macos) {
    if (!macos && macos_only(item.role)) {
        return false;
    }
    // Nothing to do without an action: About and Settings elsewhere.
    if (!macos && !item.action && (item.role == MenuRole::about || item.role == MenuRole::settings)) {
        return false;
    }
    return true;
}

// Resolves `item` (pre-order position `id`), advancing `id` past it and its submenu.
Item resolve_item(const MenuBarItem& item, std::uint32_t& id, bool macos) {
    Item result;
    result.id = id++;
    result.title = item.title.empty() ? standard_title(item.role, macos) : item.title;
    result.separator = item.title.empty() && item.role == MenuRole::none && item.submenu.empty();
    result.enabled = item.enabled;
    result.checked = item.checked;
    result.shortcut = item.shortcut ? item.shortcut : standard_shortcut(item.role);
    result.role = item.role;
    result.has_action = !(macos && !item.action && native_on_macos(item.role));
    for (const MenuBarItem& child : item.submenu) {
        if (keeps(child, macos)) {
            result.submenu.push_back(resolve_item(child, id, macos));
        } else {
            id += 1;
            for (std::vector<const MenuBarItem*> stack{&child}; !stack.empty();) {
                const MenuBarItem* next = stack.back();
                stack.pop_back();
                for (const MenuBarItem& grandchild : next->submenu) {
                    ++id;
                    stack.push_back(&grandchild);
                }
            }
        }
    }
    return result;
}

// Drops separators at the ends and in a row, which leaving items out can make.
void tidy(std::vector<Item>& items) {
    std::vector<Item> kept;
    for (Item& item : items) {
        tidy(item.submenu);
        if (item.separator && (kept.empty() || kept.back().separator)) {
            continue;
        }
        kept.push_back(std::move(item));
    }
    while (!kept.empty() && kept.back().separator) {
        kept.pop_back();
    }
    items = std::move(kept);
}

std::string_view key_name(Key key) {
    static constexpr std::string_view letters[] = {"A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M",
                                                   "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z"};
    static constexpr std::string_view digits[] = {"0", "1", "2", "3", "4", "5", "6", "7", "8", "9"};
    static constexpr std::string_view functions[] = {"F1", "F2", "F3", "F4",  "F5",  "F6",
                                                     "F7", "F8", "F9", "F10", "F11", "F12"};
    const int k = static_cast<int>(key);
    if (key >= Key::a && key <= Key::z) {
        return letters[k - static_cast<int>(Key::a)];
    }
    if (key >= Key::digit0 && key <= Key::digit9) {
        return digits[k - static_cast<int>(Key::digit0)];
    }
    if (key >= Key::f1 && key <= Key::f12) {
        return functions[k - static_cast<int>(Key::f1)];
    }
    switch (key) {
    case Key::enter: return "Enter";
    case Key::escape: return "Esc";
    case Key::backspace: return "Backspace";
    case Key::tab: return "Tab";
    case Key::space: return "Space";
    case Key::forward_delete: return "Del";
    case Key::insert: return "Ins";
    case Key::home: return "Home";
    case Key::end: return "End";
    case Key::page_up: return "PgUp";
    case Key::page_down: return "PgDn";
    case Key::left: return "Left";
    case Key::right: return "Right";
    case Key::up: return "Up";
    case Key::down: return "Down";
    default: return "";
    }
}

} // namespace

std::vector<Item> resolve(const std::vector<MenuBarItem>& menus, bool macos) {
    std::vector<Item> result;
    std::vector<Item> app_items; // elsewhere, the app menu's items join the first menu
    std::uint32_t id = 1;
    for (const MenuBarItem& menu : menus) {
        Item resolved = resolve_item(menu, id, macos);
        if (!macos && menu.role == MenuRole::app) {
            app_items.insert(app_items.end(), std::make_move_iterator(resolved.submenu.begin()),
                             std::make_move_iterator(resolved.submenu.end()));
            continue;
        }
        result.push_back(std::move(resolved));
    }
    if (!app_items.empty()) {
        if (result.empty()) {
            result.push_back({.title = "File"});
        }
        std::vector<Item>& first = result.front().submenu;
        if (!first.empty()) {
            app_items.push_back({.separator = true});
        }
        first.insert(first.begin(), std::make_move_iterator(app_items.begin()), std::make_move_iterator(app_items.end()));
    }
    tidy(result);
    return result;
}

const MenuBarItem* find(const std::vector<MenuBarItem>& menus, std::uint32_t id) {
    std::uint32_t at = 1;
    std::vector<const MenuBarItem*> stack;
    for (auto it = menus.rbegin(); it != menus.rend(); ++it) {
        stack.push_back(&*it);
    }
    while (!stack.empty()) {
        const MenuBarItem* item = stack.back();
        stack.pop_back();
        if (at++ == id) {
            return item;
        }
        for (auto it = item->submenu.rbegin(); it != item->submenu.rend(); ++it) {
            stack.push_back(&*it);
        }
    }
    return nullptr;
}

std::optional<KeyboardShortcut> standard_shortcut(MenuRole role) {
    const bool macos = current_platform() == Platform::macos;
    const KeyModifiers primary = primary_modifier();
    switch (role) {
    case MenuRole::quit:
        if (current_platform() == Platform::windows) {
            return std::nullopt; // Alt+F4 is the system's
        }
        return KeyboardShortcut{Key::q, primary};
    case MenuRole::hide: return KeyboardShortcut{Key::h, primary};
    case MenuRole::hide_others: return KeyboardShortcut{Key::h, primary | KeyModifiers::alt};
    case MenuRole::undo: return KeyboardShortcut{Key::z, primary};
    case MenuRole::redo:
        return current_platform() == Platform::windows ? KeyboardShortcut{Key::y, primary}
                                                       : KeyboardShortcut{Key::z, primary | KeyModifiers::shift};
    case MenuRole::cut: return KeyboardShortcut{Key::x, primary};
    case MenuRole::copy: return KeyboardShortcut{Key::c, primary};
    case MenuRole::paste: return KeyboardShortcut{Key::v, primary};
    case MenuRole::select_all: return KeyboardShortcut{Key::a, primary};
    case MenuRole::minimize:
        return macos ? std::optional(KeyboardShortcut{Key::m, primary}) : std::nullopt;
    case MenuRole::toggle_fullscreen: return KeyboardShortcut{Key::f, primary | KeyModifiers::control};
    default: return std::nullopt;
    }
}

std::optional<KeyboardShortcut> edit_key(MenuRole role) {
    const KeyModifiers primary = primary_modifier();
    switch (role) {
    case MenuRole::undo: return KeyboardShortcut{Key::z, primary};
    case MenuRole::redo: return KeyboardShortcut{Key::z, primary | KeyModifiers::shift};
    case MenuRole::cut: return KeyboardShortcut{Key::x, primary};
    case MenuRole::copy: return KeyboardShortcut{Key::c, primary};
    case MenuRole::paste: return KeyboardShortcut{Key::v, primary};
    case MenuRole::select_all: return KeyboardShortcut{Key::a, primary};
    default: return std::nullopt;
    }
}

std::string shortcut_text(const KeyboardShortcut& shortcut) {
    std::string text;
    if (has(shortcut.modifiers, KeyModifiers::control)) {
        text += "Ctrl+";
    }
    if (has(shortcut.modifiers, KeyModifiers::alt)) {
        text += "Alt+";
    }
    if (has(shortcut.modifiers, KeyModifiers::shift)) {
        text += "Shift+";
    }
    if (has(shortcut.modifiers, KeyModifiers::super)) {
        text += current_platform() == Platform::windows ? "Win+" : "Super+";
    }
    text += key_name(shortcut.key);
    return text;
}

std::uint32_t match_shortcut(const std::vector<Item>& menus, const KeyboardShortcut& shortcut) {
    for (const Item& item : menus) {
        if (!item.enabled) {
            continue;
        }
        if (item.submenu.empty() && item.shortcut && *item.shortcut == shortcut) {
            return item.id;
        }
        if (const std::uint32_t found = match_shortcut(item.submenu, shortcut)) {
            return found;
        }
    }
    return 0;
}

namespace {

View menu_entries(const std::vector<Item>& items, const std::function<void(std::uint32_t)>& command,
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
            view = MenuItem(item.title, [command, id = item.id] { command(id); }, std::move(options));
        }
        views.push_back(item.enabled ? std::move(view) : view.disabled());
    }
    return detail::make_menu(std::move(views));
}

} // namespace

View menu_bar_view(Prop<std::vector<Item>> menus, std::function<void(std::uint32_t)> command,
                   std::function<bool(std::uint32_t)> checked) {
    const auto indexed = [menus] {
        std::vector<std::pair<std::size_t, Item>> result;
        const std::vector<Item> items = menus.get();
        for (std::size_t i = 0; i < items.size(); ++i) {
            result.emplace_back(i, items[i]);
        }
        return result;
    };
    const float infinity = std::numeric_limits<float>::infinity();
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
