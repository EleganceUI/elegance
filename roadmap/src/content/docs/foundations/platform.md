---
title: Platform layer (SDL3)
description: Windows, events, DPI, text input, clipboard, cursors, and the system theme behind an internal interface.
sidebar:
  order: 3
---

SDL3 does the OS work, but no SDL type may appear outside `src/platform/`. Everything above talks
to a small internal interface, so a native backend could replace SDL later.

:::note[Depends on]
[Errors, logging, and crash reports](../core/).
:::

## Read first

- `design.md` §4 (Platform Layer), §15 (UI thread = main thread)
- `src/platform/platform.h`, `src/platform/platform_sdl.cpp`, `src/platform/dynamic_library.*`

## Tasks

### The interface
- [ ] `Platform` owns SDL's lifetime. Construct it on the main thread, which becomes the UI thread
- [ ] `Platform::vulkan_instance_extensions()`
- [ ] `wait_event()`, `wait_event(seconds)` (returns `none` on timeout), `poll_event()`
- [ ] `wake()`, thread-safe: a custom SDL event that wakes `wait_event`
- [ ] Translate SDL events into one internal `Event` struct: quit, wake, exposed, resized, scale changed, close requested, pointer move/down/up/leave, wheel (with a `precise` flag for trackpads), text input, text editing (IME composition and caret), key down/up (layout-mapped `Key`, modifiers, repeat), maximized/restored, focus gained/lost, moved, system theme changed, and drop begin/position/file/text/complete

### Windows
- [ ] `Window(WindowDesc)` created hidden; `show()` only after the first frame is queued
- [ ] `pixel_size()`, `scale()` (pixels per point), `logical_size()`, `to_logical(x, y)`
- [ ] `create_surface(VkInstance)`. The caller owns the surface and destroys it before the window
- [ ] `native_handle()` (HWND on Windows)
- [ ] `minimize()`, `toggle_maximized()`, `maximized()`, `request_close()`
- [ ] Text input: `start_text_input()`, `stop_text_input()`, `set_text_input_area(rect, cursor)`
- [ ] `usable_area()`: the display's work area in the window's logical points (popups need it)

### Process-wide helpers
- [ ] Clipboard text get/set
- [ ] Cursor shape (arrow, I-beam)
- [ ] `system_prefers_dark()` plus the theme-changed event
- [ ] `pref_path()`, `environment_variable()`, `debugger_present()`, `debug_break()`, `show_error_box()`
- [ ] `any_window_focused()`
- [ ] `wait_for_compositor()` (`DwmFlush` on Windows, nothing elsewhere)
- [ ] `DynamicLibrary` (LoadLibrary/dlopen) for hot reload and optional system DLLs

### Live resize
- [ ] `set_live_resize_callback(fn)`: called on the main thread from inside the OS modal size/move loop, when the normal event loop is blocked. It must not throw
- [ ] Trigger it from SDL's expose event watch, **and** on every pixel-size change while in the OS move/size loop (`GUI_INMOVESIZE`)

## Done when

- [ ] A window opens at the right size on a 150% display, and `scale()` reports 1.5
- [ ] Moving the window between monitors with different scales sends a scale-changed event
- [ ] An idle app blocked in `wait_event()` uses 0% CPU, and `wake()` from another thread returns it
- [ ] No `#include <SDL3/...>` outside `src/platform/`

## Gotchas

- **SDL's live-resize timer is a `WM_TIMER`**, which Windows delivers only when no other message
  is waiting. During fast drags it fired very irregularly (16 frames in one drag vs 55 in another,
  with 100–200 px jumps). Draw on every pixel-size change inside the move/size loop as well.
- SDL requires surfaces to be created and destroyed on the main thread, even though the render
  thread uses them.
- Popups never become the active window, so keyboard focus and text input stay on their parent.
  Start text input on the parent when a popup needs it.
