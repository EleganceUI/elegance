// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// The menu bar on macOS (App::menu_bar): the app's main menu. Its key equivalents work on their
// own. Roles without an action of their own use AppKit's standard actions (About, Hide, the
// window's Minimize, Zoom, Full Screen). Compiled with ARC.

#include "platform/generic/native.h"

#import <AppKit/AppKit.h>

#include <map>

// Sends chosen items' ids (tags) to the app.
@interface ElgMenuTarget : NSObject
@property(nonatomic) elg::detail::platform::native::MenuCommand* command;
- (void)choose:(NSMenuItem*)sender;
@end

@implementation ElgMenuTarget
- (void)choose:(NSMenuItem*)sender {
    if (self.command && *self.command) {
        (*self.command)(static_cast<std::uint32_t>(sender.tag));
    }
}
@end

namespace elg::detail::platform::native {

namespace {

MenuCommand g_command;
ElgMenuTarget* g_target = nil;
std::map<std::uint32_t, NSMenuItem*> g_items; // by id, for check marks

NSString* ns(const std::string& text) {
    NSString* result = [NSString stringWithUTF8String:text.c_str()];
    return result ? result : @"";
}

NSString* app_name() {
    NSString* name = NSRunningApplication.currentApplication.localizedName;
    return name ? name : NSProcessInfo.processInfo.processName;
}

// AppKit's key equivalent for `key`, or empty.
NSString* key_equivalent(Key key) {
    if (key >= Key::a && key <= Key::z) {
        const unichar c = static_cast<unichar>('a' + (static_cast<int>(key) - static_cast<int>(Key::a)));
        return [NSString stringWithCharacters:&c length:1];
    }
    if (key >= Key::digit0 && key <= Key::digit9) {
        const unichar c = static_cast<unichar>('0' + (static_cast<int>(key) - static_cast<int>(Key::digit0)));
        return [NSString stringWithCharacters:&c length:1];
    }
    if (key >= Key::f1 && key <= Key::f12) {
        const unichar c = static_cast<unichar>(NSF1FunctionKey + (static_cast<int>(key) - static_cast<int>(Key::f1)));
        return [NSString stringWithCharacters:&c length:1];
    }
    unichar c = 0;
    switch (key) {
    case Key::enter: c = '\r'; break;
    case Key::escape: c = 0x1b; break;
    case Key::backspace: c = 0x08; break;
    case Key::tab: c = '\t'; break;
    case Key::space: c = ' '; break;
    case Key::forward_delete: c = NSDeleteFunctionKey; break;
    case Key::insert: c = NSInsertFunctionKey; break;
    case Key::home: c = NSHomeFunctionKey; break;
    case Key::end: c = NSEndFunctionKey; break;
    case Key::page_up: c = NSPageUpFunctionKey; break;
    case Key::page_down: c = NSPageDownFunctionKey; break;
    case Key::left: c = NSLeftArrowFunctionKey; break;
    case Key::right: c = NSRightArrowFunctionKey; break;
    case Key::up: c = NSUpArrowFunctionKey; break;
    case Key::down: c = NSDownArrowFunctionKey; break;
    default: return @"";
    }
    return [NSString stringWithCharacters:&c length:1];
}

NSEventModifierFlags modifier_mask(KeyModifiers modifiers) {
    NSEventModifierFlags mask = 0;
    if (has(modifiers, KeyModifiers::super)) {
        mask |= NSEventModifierFlagCommand;
    }
    if (has(modifiers, KeyModifiers::control)) {
        mask |= NSEventModifierFlagControl;
    }
    if (has(modifiers, KeyModifiers::alt)) {
        mask |= NSEventModifierFlagOption;
    }
    if (has(modifiers, KeyModifiers::shift)) {
        mask |= NSEventModifierFlagShift;
    }
    return mask;
}

// AppKit's own action for a role, for items without an action of their own.
SEL standard_action(MenuRole role) {
    switch (role) {
    case MenuRole::about: return @selector(orderFrontStandardAboutPanel:);
    case MenuRole::hide: return @selector(hide:);
    case MenuRole::hide_others: return @selector(hideOtherApplications:);
    case MenuRole::show_all: return @selector(unhideAllApplications:);
    case MenuRole::minimize: return @selector(performMiniaturize:);
    case MenuRole::zoom: return @selector(performZoom:);
    case MenuRole::toggle_fullscreen: return @selector(toggleFullScreen:);
    default: return nullptr;
    }
}

NSString* title_of(const menus::Item& item) {
    if (!item.title.empty()) {
        return ns(item.title);
    }
    switch (item.role) {
    case MenuRole::app: return app_name();
    case MenuRole::about: return [@"About " stringByAppendingString:app_name()];
    case MenuRole::hide: return [@"Hide " stringByAppendingString:app_name()];
    case MenuRole::quit: return [@"Quit " stringByAppendingString:app_name()];
    default: return @"";
    }
}

NSMenu* build_menu(NSString* title, const std::vector<menus::Item>& items);

NSMenuItem* build_item(const menus::Item& item) {
    if (item.separator) {
        return NSMenuItem.separatorItem;
    }
    NSMenuItem* result = [[NSMenuItem alloc] initWithTitle:title_of(item) action:nil keyEquivalent:@""];
    if (!item.submenu.empty() || item.role == MenuRole::app || item.role == MenuRole::window ||
        item.role == MenuRole::help) {
        result.submenu = build_menu(result.title, item.submenu);
        if (item.role == MenuRole::window) {
            NSApp.windowsMenu = result.submenu;
        } else if (item.role == MenuRole::help) {
            NSApp.helpMenu = result.submenu;
        }
        return result;
    }
    if (item.has_action) {
        result.target = g_target;
        result.action = @selector(choose:);
        result.tag = static_cast<NSInteger>(item.id);
    } else {
        result.action = standard_action(item.role); // nil target: the key window or the app
    }
    if (item.shortcut) {
        result.keyEquivalent = key_equivalent(item.shortcut->key);
        result.keyEquivalentModifierMask = modifier_mask(item.shortcut->modifiers);
    } else if (item.role == MenuRole::settings) {
        result.keyEquivalent = @",";
        result.keyEquivalentModifierMask = NSEventModifierFlagCommand;
    }
    result.enabled = item.enabled;
    if (item.checked) {
        result.state = *item.checked ? NSControlStateValueOn : NSControlStateValueOff;
    }
    if (item.id != 0) {
        g_items[item.id] = result;
    }
    return result;
}

NSMenu* build_menu(NSString* title, const std::vector<menus::Item>& items) {
    NSMenu* menu = [[NSMenu alloc] initWithTitle:title];
    menu.autoenablesItems = NO; // items say whether they're enabled
    for (const menus::Item& item : items) {
        [menu addItem:build_item(item)];
    }
    return menu;
}

// The app menu macOS expects first, when the app gave none.
NSMenuItem* standard_app_menu() {
    NSMenu* menu = [[NSMenu alloc] initWithTitle:app_name()];
    [menu addItemWithTitle:[@"About " stringByAppendingString:app_name()]
                    action:@selector(orderFrontStandardAboutPanel:)
             keyEquivalent:@""];
    [menu addItem:NSMenuItem.separatorItem];
    [menu addItemWithTitle:[@"Hide " stringByAppendingString:app_name()] action:@selector(hide:) keyEquivalent:@"h"];
    NSMenuItem* others = [menu addItemWithTitle:@"Hide Others" action:@selector(hideOtherApplications:) keyEquivalent:@"h"];
    others.keyEquivalentModifierMask = NSEventModifierFlagCommand | NSEventModifierFlagOption;
    [menu addItemWithTitle:@"Show All" action:@selector(unhideAllApplications:) keyEquivalent:@""];
    [menu addItem:NSMenuItem.separatorItem];
    // The app hears it as a quit request (SDL_EVENT_QUIT).
    [menu addItemWithTitle:[@"Quit " stringByAppendingString:app_name()] action:@selector(terminate:) keyEquivalent:@"q"];
    NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:app_name() action:nil keyEquivalent:@""];
    item.submenu = menu;
    return item;
}

} // namespace

bool draws_menu_bar_in_content() {
    return false;
}

bool menu_bar_handles_shortcuts() {
    return true;
}

void set_menu_bar(const std::vector<menus::Item>& menus, MenuCommand command) {
    g_command = std::move(command);
    if (!g_target) {
        g_target = [ElgMenuTarget new];
    }
    g_target.command = &g_command;
    g_items.clear();
    NSMenu* bar = [[NSMenu alloc] initWithTitle:@""];
    if (menus.empty() || menus.front().role != MenuRole::app) {
        [bar addItem:standard_app_menu()];
    }
    for (const menus::Item& menu : menus) {
        [bar addItem:build_item(menu)];
    }
    NSApp.mainMenu = bar;
}

void set_menu_checked(std::uint32_t id, bool checked) {
    if (const auto found = g_items.find(id); found != g_items.end()) {
        found->second.state = checked ? NSControlStateValueOn : NSControlStateValueOff;
    }
}

} // namespace elg::detail::platform::native
