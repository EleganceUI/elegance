---
title: Desktop integration
description: "File dialogs with async results, drop targets found by hit testing, shell notifications, and the tray icon."
sidebar:
  order: 1
depends: [framework/input-and-focus, graphics/images]
---

## What it's for

These are the OS features people otherwise reach for Electron for: opening and saving files,
accepting dragged files, showing notifications, and living in the tray. Each one is built on SDL3
where it covers the feature, and on the platform directly where it doesn't.

## How it works

### File dialogs

`choose_files({.filters, .directory, .multiple}, done)`, `choose_save_file({.filters, .directory,
.name}, done)` and `choose_folders(...)` open native dialogs (SDL3's, which on Windows are the
system file picker) over the focused window.

- The **filters** (name + patterns) are copied into storage that stays alive until the dialog
  closes, because SDL may read them until then.
- SDL calls back **possibly on another thread** when the dialog closes. The callback copies the
  result (paths, or cancelled) and posts a UI task that runs `done`. The UI never blocks on a
  dialog.

### Drag and drop in

`.on_drop({.files, .text, .targeted})` makes a view a **drop target**. The view's element passes
layout, paint and hit testing through to its content, and only adds target behavior.

The App tracks a drag per window from SDL3's drop events:

1. **begin**: a drag entered the window.
2. **position**: find the **innermost drop target at the point that accepts the kind** being
   dragged (files or text): search children first, and among siblings the later ones (drawn on
   top) first. Remember it **by path** (child indices from the root), not by pointer, so a tree
   change mid-drag can't leave a dangling target. Set its `targeted` binding true (and the
   previous target's false) to drive a highlight.
3. **file / text**: one event per item; collect them.
4. **complete**: deliver the collected files or text to the current target, with the drop location
   **in the target's own coordinates**, and clear `targeted`. A drag that leaves without dropping
   just clears.

### Notifications

SDL has no notifications, so Windows uses the shell's notification icon directly:

- A **message-only window** (never shown) receives the icon's messages.
- `notify({.title, .body, .on_click})` adds a notification icon (with the app's own icon from its
  executable, else the system's) and shows a balloon (`Shell_NotifyIcon` with `NIF_INFO`). Windows
  10 and 11 show it as a toast and keep it in the notification center.
- A click arrives as `NIN_BALLOONUSERCLICK`, which runs `on_click` on the UI thread.
- The icon is removed when the notification is done.

Rich toasts (buttons, images) need WinRT, an AppUserModelID and a Start menu shortcut, so they're
left for packaged apps.

### Tray icon

`TrayIcon({.icon, .tooltip, .menu})` uses SDL3's tray. The menu is a description of buttons,
checkboxes, separators and submenus. Each entry with something to do is kept **at a stable
address**, because SDL keeps pointers to its callback data. Its callbacks post UI tasks.
`set_tooltip` and `set_menu` update it. The icon comes from an `ImageSource`, decoded to pixels,
or a plain blue dot. The tray icon shows while any `TrayIcon` copy lives.

## Rules

- Every OS callback hops to the UI thread before touching app code.
- Hold drop targets by path, never by pointer.
- Keep any data SDL points at alive until SDL is done with it.

## Edge cases

- **Dropping on a view that only takes text** with files ignores it; the search continues outward
  to a target that takes files.
- **Cancelling a dialog** calls `done` with an empty list (`choose_files`, `choose_folders`) or
  `nullopt` (`choose_save_file`).
- **Interactive parts can't be tested automatically**: dialogs, toasts, the tray and drops need a
  person to try them.
