---
title: Platform layer (SDL3)
description: "The internal OS interface: windows, one event type, waking, live resize, and process services."
sidebar:
  order: 3
depends: [foundations/core]
---

## What it's for

Everything above this layer needs windows, input, a Vulkan surface, the clipboard, text input
and a few OS facts (scale, dark mode, cache folders). SDL3 provides all of that on every target.
This layer wraps it in a small interface of Elegence's own, so no other code sees SDL, and a
native backend could replace it later without touching the rest.

## How it works

### Threads

SDL requires its calls on the main thread. The thread that constructs `Platform` becomes the
**UI thread** for the life of the app: events, reactivity, layout and painting all happen there.
Window surfaces are also created and destroyed there, even though the render thread draws into
them.

### Events

`platform::Event` is one plain struct for every kind of event. A `type` field tags it, and the
other fields are filled per type:

| Types | Fields |
|---|---|
| Pointer move/down/up/leave | `x`, `y` (window coordinates), `button`, `clicks`, `modifiers` |
| Wheel | `wheel_x`, `wheel_y` in notches (fractional on trackpads), `precise` |
| Text input / editing (IME) | `text`, `text_cursor` (bytes) |
| Key down/up | `key` (layout-mapped), `modifiers`, `repeat` |
| Window | exposed, resized, scale changed, close requested, maximized/restored, focus gained/lost, moved |
| System | quit, wake, system theme changed |
| Drop | begin, position (`x`, `y`), file / text (`text`), complete |

The loop blocks in `wait_event()`, or in `wait_event(seconds)` when something is due (a hover
delay, a caret blink, an animation). An idle app sits in a blocking OS wait and uses no CPU.

`wake()` is the one thread-safe call. It pushes a custom SDL event, so a blocked `wait_event`
returns `EventType::wake`. Glyph workers, `post_ui_task`, and the render thread's error path all
end in `wake()`.

### Windows

`Window(desc, popup?)` creates an SDL window **hidden**. The App shows it only after its first
frame is on screen, so windows never flash blank. Each window reports:

- `pixel_size()` and `scale()`, where scale is pixels per logical point. Layout works in points,
  and the renderer multiplies by scale.
- `to_logical(x, y)`, which converts pointer coordinates.
- `create_surface(instance)`, a `VkSurfaceKHR` owned by the caller and destroyed before the window.
- `native_handle()` (the HWND), needed for DirectComposition, AccessKit and media interop.

Popups are created with `SDL_CreatePopupWindow` relative to a parent, and positioned with
`set_popup_frame(rect)` in the parent's points. Popups never take keyboard focus, so text input
for a popup is started on its parent. `usable_area()` gives the display's work area in the
window's points, which popup placement needs.

Undecorated windows install `set_hit_test(fn)`. SDL calls it during event processing with a point,
and it returns `normal`, `drag`, or one of eight resize edges. Returning `drag` gives native
behavior: double-click maximizes, and Aero Snap works.

### Live resize

On Windows, dragging a window edge enters a modal OS loop, and the app's own event loop stops
running until the drag ends. Two hooks keep frames coming:

1. SDL's expose event watch fires during the modal loop.
2. Elegence also triggers on every pixel-size change while the thread is in the move/size loop
   (`GUI_INMOVESIZE`).

Both call `dispatch_live_resize(window_id)`, which runs the callback the App registered: lay out,
post a frame, wait for it to be presented, then `wait_for_compositor()` (`DwmFlush`). The new
frame is therefore on screen before the OS resizes the window again, so nothing stretches. The
callback must not throw, because it runs inside an OS callback.

### Process services

Free functions cover what isn't per-window: clipboard text, the cursor shape (arrow or I-beam),
`system_prefers_dark()`, `pref_path()` (the per-user data folder), environment variables,
debugger detection, `show_error_box`, `any_window_focused()`. `DynamicLibrary` loads DLLs at
runtime, for hot-reload modules and for optional system libraries such as the WebView2 loader.

## Rules

- No SDL header is included outside `src/platform/`.
- Everything except `wake()` runs on the UI thread.
- Coordinates leave this layer either in window coordinates with a conversion function, or
  already in logical points. Physical pixels are only for surfaces.

## Edge cases

- **SDL's live-resize timer is a `WM_TIMER`.** Windows delivers it only when no other message is
  waiting, so during fast drags it fired very unevenly (16 frames in one drag, 55 in another,
  with 100–200 px jumps). That's why the size-change trigger exists.
- **Scale changes** (moving between monitors) arrive as their own event. Everything cached in
  pixels has to be rebuilt.
- **Minimized windows** report a zero pixel size. Swapchains skip them rather than fail.
