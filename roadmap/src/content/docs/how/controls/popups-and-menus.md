---
title: Popups, menus, and tooltips
description: "The popup presentation layer: requests, popup windows, placement, menu input, submenus, popovers and tooltips."
sidebar:
  order: 3
depends: [controls/basic-controls, controls/windows]
---

## What it's for

Menus, dropdowns, popovers and tooltips must escape their window's bounds, look like the rest of
the app, and behave like native menus: keyboard navigation, submenus, dismissal. Elegence draws
them itself, inside small popup windows.

## How it works

### Why popup windows

Native menus can't host custom views or be styled, and Linux has no common native menu. Drawing
inside the main window gets clipped near its edges. A popup window per open popup gives full
styling and correct placement (Wayland via `xdg_popup`). The cost is a small swapchain per popup.

To the framework, a popup is **just another window**: its own element tree, scope, focus manager,
event dispatcher and render target.

### Requests

Views never open windows directly. They call `WindowServices`:

- `open_popup(PopupRequest)`: kind (menu, tooltip, popover), placement (`at_point`, `below`,
  `beside`, `above`, `leading`, optionally `centered`), an anchor rect in the opener's points, a
  content builder, the opener's environment, a level, and `on_close`.
- `close_menus(level)`, `close_menus_then(action)`, `close_tooltip()`.

Requests are **queued** and carried out between events and layout, never during event handling.
Each loop iteration runs closes first, then actions of chosen items, then opens.

### Opening a popup

1. Create a borderless, transparent `SDL_CreatePopupWindow` child of the opener (submenus are
   children of their menu).
2. Make a scope, lower `content()` inside it with the opener's environment (theme, fonts and tint
   carry over), and add room around the panel for its shadow.
3. **Size to the content**: measure with an unspecified proposal.
4. **Place** by the anchor and placement. If it doesn't fit the display's usable area, flip to the
   other side; then shift it to stay inside. Submenus overlap their row slightly.
5. Show it after its first frame.

Popups are transparent, so on Windows they present through DirectComposition, which gives them
rounded corners and shadows over the desktop.

### The menu stack

Open menus form a stack by **level**: 0 for the first, +1 per submenu. Opening at level n closes
levels ≥ n first. Popovers share this stack and its closing rules.

While any menu is open:

- **Keys go to the top menu**: Up/Down/Home/End move focus between rows (the dispatcher's menu
  navigation mode), Enter/Space choose, Esc closes the top menu, Left closes a submenu, Right
  opens one.
- **A press outside** every menu closes them all, and the click isn't passed through.
- **Moving or resizing** the parent window, or the app losing focus, closes them.

### Menu contents

`Menu(items...)` is a vertical stack of rows. `MenuItem(title, action, {.shortcut, .checked,
.destructive, .image})` is a Button in a row style: highlighted when **hovered or focused**, since
hovering a row focuses it and the two highlights are one. Menu popups have **zero hover delay**,
like native menus.

Choosing an item calls `close_menus_then(action)`: every menu closes, **then** the action runs,
so it can open a dialog or another menu. The builder runs on every open in a fresh scope, so items
reflect current state and nothing stays subscribed while closed.

`Submenu(title, builder)` is a row that opens a menu `beside` itself at level + 1: immediately on
click, Enter, Space or Right, or after the pointer **rests 0.25 s** on it, so sweeping across rows
doesn't flash submenus open. Hovering other rows closes deeper submenus, except the submenu's own
row, which would otherwise close what it just opened.

### Openers

- `.context_menu(builder)`: right-button **release** (the press is consumed so nothing else
  reacts), the Menu key, or Shift+F10. Opens `at_point`.
- `MenuButton(label, builder)`: a Button that opens a menu `below` it.
- `Picker(..., PickerStyle::menu)`: a MenuButton whose items are checked options.
- `.help(text)`: a tooltip after the pointer rests 0.6 s; closed by leaving, pressing, scrolling
  or typing.

### Popovers

`.popover(Binding<bool>, content, {.opens})` opens a panel beside its view while the binding is
true, and closes it when it turns false. Closing from outside (a click elsewhere, Esc) sets the
binding back to false. A generation counter makes sure a late close callback can't close a newer
popover. Opening waits for the view's first frame, so there's something to anchor to.

## Rules

- Popups open and close only between events.
- Menu actions run after the menus are gone.
- One highlight per menu: hover moves focus.
- Closing for any reason runs `on_close` exactly once.

## Edge cases

- **Opening is slow-ish**: each popup builds a D3D device and swapchain. The plan is to keep one
  popup window ready and reuse it.
- **Diagonal moves toward a submenu** cross other rows and close it. Native menus use a "safe
  triangle"; that's a follow-up.
- **Embedding mode** has no platform windows; popups there would fall back to an in-window top
  layer.
