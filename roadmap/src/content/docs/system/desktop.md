---
title: Desktop integration
description: File dialogs, drag and drop in, notifications, and the tray icon.
sidebar:
  order: 1
---

The things apps otherwise reach for Electron's APIs for, built on the platform (SDL3 where it
covers them).

:::note[Depends on]
[Platform layer](../../foundations/platform/), [Hit testing, events, and focus](../../framework/input-and-focus/),
and [Raster images](../../graphics/images/) (tray icons).
:::

## Read first

- `design.md` §13.4 (Desktop integration), M5 desktop items
- `include/elg/desktop.h`, `src/platform/desktop_sdl.cpp`, `src/view/drop.*`, `tests/desktop_tests.cpp`, `examples/desktop/main.cpp`

## Tasks

### File dialogs
- [ ] `choose_files({.filters, .directory, .multiple})`, `choose_save_file({.filters, .directory, .name})`, `choose_folders(...)`
- [ ] Native dialogs over the focused window; results reach the UI thread asynchronously

### Drag and drop in
- [ ] `.on_drop({.files, .text, .targeted})` on any view
- [ ] The innermost view under the pointer that accepts that kind gets the drop, with the location in its own coordinates
- [ ] `targeted` drives a highlight while a drag hovers
- [ ] Use SDL3's drop events (begin, position, file, text, complete); hold the targeted view **by path**, so tree changes mid-drag are safe

### Notifications
- [ ] `notify({.title, .body, .on_click})`
- [ ] Windows: a message-only window plus `Shell_NotifyIcon` with `NIF_INFO`, shown as a toast with the app's icon and kept in the notification center; clicks arrive through `NIN_BALLOONUSERCLICK`

### Tray icon
- [ ] `TrayIcon({.icon, .tooltip, .menu})` on SDL3's tray: buttons, check boxes, separators and submenus
- [ ] Shown while any copy lives; `set_tooltip`, `set_menu`; the icon comes from an `ImageSource`, else a plain dot

## Done when

- [ ] Dropping files from Explorer onto a target highlights it while hovering, then delivers the paths
- [ ] A notification appears as a toast and its click runs the handler
- [ ] The tray menu's check box toggles app state
- [ ] A person has tried the dialogs, toasts, tray and drops by hand (this can't be automated, and the design notes it still needs doing)

## Gotchas

- Proper WinRT toasts (actions, images) need an AppUserModelID and a Start-menu shortcut, so
  they're a follow-up for packaged apps.
- Follow-ups: dragging out to other apps, badge counts, the window icon and jump lists, "open
  with" / file associations, single-instance apps, and clipboard images and files.
