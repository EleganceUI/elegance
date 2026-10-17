---
title: Popups, menus, and tooltips
description: Elegence-drawn menus in SDL popup windows, keyboard navigation, submenus, and tooltips.
sidebar:
  order: 3
---

Menus, dropdowns, popovers and tooltips are drawn by Elegence into real popup windows, so they
can extend past the main window. To the framework, a popup is just another window with its own
tree.

:::note[Depends on]
[Basic controls](../basic-controls/) and the transparency path in [Windows and title bars](../windows/)
(popups are transparent, for rounded panels and shadows).
:::

## Read first

- `design.md` §13.1 (Popups and context menus), M4 "Popup presentation layer"
- `src/view/menus.cpp`, `PopupRequest` and `WindowServices` in `src/view/window_services.h`, popup handling in `src/app.cpp`, `src/view/icons.h`

## Tasks

### Presentation layer
- [ ] Popups are live windows made with `SDL_CreatePopupWindow` (`SDL_WINDOW_POPUP_MENU` / `SDL_WINDOW_TOOLTIP`), borderless and transparent
- [ ] `PopupRequest { kind (menu, tooltip, popover), placement (at_point, below, beside, above, leading), centered, anchor, content builder, environment, level, on_close }`
- [ ] `WindowServices::open_popup`, `close_menus(level)`, `close_menus_then(action)`, `close_tooltip`, `menu_level`
- [ ] Queue requests and carry them out between events and layout, never while an event is being handled
- [ ] Placement flips and shifts to stay within the display's usable area
- [ ] Extra window room around the panel for its shadow
- [ ] Popups carry the opener's environment (theme, fonts, tint)

### Menus
- [ ] `Menu(items...)`, `MenuItem(title, action, {.shortcut, .destructive, .checked, .image})`, `Submenu(title, builder)`, `Divider()`
- [ ] The builder runs each time the menu opens, in a fresh scope disposed on close
- [ ] Choosing an item closes every menu, **then** runs its action (so it can open a dialog or another menu)
- [ ] While a menu is open, keys go to the top menu: Up/Down/Home/End move, Enter/Space choose, Esc closes, Left leaves a submenu, Right enters one
- [ ] A press elsewhere closes the menus without passing the click on
- [ ] Moving or resizing the window, or the app losing focus, closes them
- [ ] Hovering a row focuses it, so the hover and keyboard highlights stay one
- [ ] Menu popups have zero hover delay; submenus open after resting 0.25 s

### Openers
- [ ] `.context_menu(builder)`: right-click (on release on Windows), the Menu key, Shift+F10
- [ ] `MenuButton(label, builder)` and `Picker(..., PickerStyle::menu)`
- [ ] `.help("...")` tooltips: after 0.6 s; closed by leaving, pressing, scrolling, or typing

### Popovers
- [ ] `.popover(Binding<bool> is_presented, content, {.opens = PopoverEdge::...})`: opens by the view while the binding is true, closes when it goes false
- [ ] Popovers share the menus' stack and closing rules (outside click, Esc, focus loss) but not menu keyboard navigation
- [ ] Closed from outside, the binding goes back to false; a generation counter keeps a stale close from closing a newer popover
- [ ] Opening waits for a frame, so there's a laid-out frame to anchor to
- [ ] It flips to the other side when there isn't room

## Done when

- [ ] A context menu near the screen's bottom-right corner flips to stay on screen, and extends past the window's edge
- [ ] A nested submenu works entirely by keyboard
- [ ] An item action that opens another menu works (it runs after the first menu closed)

## Gotchas

- **Composition swapchains present with interval 0**, since DWM syncs anyway. Interval 1 added a
  frame of hover latency in menus.
- Opening a popup currently builds a D3D device and swapchain each time. The planned fix is to
  keep one popup window ready and reuse it.
- Not built yet: type-to-select, diagonal-move tolerance toward submenus, `sheet`,
  `alert`, `confirmation_dialog`, `ComboBox`, `App::menu_bar` (native `NSMenu` on macOS), opening
  on press on macOS, and an in-window fallback for embedding mode.
