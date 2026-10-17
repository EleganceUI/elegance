---
title: Windows, title bars, and DirectComposition
description: WindowDesc options, custom title bars, caption buttons, transparency, and composition presentation.
sidebar:
  order: 4
---

Custom chrome needs hit testing for drag and resize, caption buttons, and (on Windows)
DirectComposition, because Vulkan swapchains there can't be transparent.

:::note[Depends on]
[Vulkan bring-up](../../renderer/vulkan/), [Platform layer](../../foundations/platform/), and
[Basic controls](../basic-controls/).
:::

## Read first

- `design.md` §4.1 (Window customization), §15 (Resize; every window through DirectComposition)
- `include/elg/window.h`, `src/view/window_controls.cpp`, `src/view/window_drag.h`, `src/gpu/composition_win32.*`
- `examples/title_bar/main.cpp`

## Tasks

### Window options and handle
- [ ] `WindowDesc`: `title`, `width`, `height`, `min_size`, `background`, `title_bar = TitleBar::hidden` (borderless), `transparent`, `opacity`
- [ ] Windows 11 rounded corners via the DWM corner preference (unless transparent)
- [ ] `WindowHandle` from `Environment<WindowKey>()`: `close()`, `minimize()`, `toggle_maximized()`, tracked `is_maximized()` and `is_active()`; it outlives the window and goes inert

### Custom title bars
- [ ] SDL hit test for undecorated windows, on the UI thread: 6 pt resize edges (unless maximized or not resizable)
- [ ] Elsewhere, the innermost handler under the point decides: a `.window_drag_area()` marker means drag (so double-click maximizes and Aero Snap works); anything else, like a button in the bar, means normal
- [ ] `WindowControls({.style, .minimize, .maximize})`: Windows-style caption buttons (46×32, red hover on close, Segoe MDL2 glyphs with drawn fallbacks) and macOS-style traffic lights; both dim while inactive and are `.focusable(false)`

### DirectComposition (Windows)
- [ ] A D3D11 device on the same adapter as Vulkan (matched by LUID)
- [ ] A shared texture created by D3D11 with an NT handle, imported into Vulkan with `VK_KHR_external_memory_win32` (UNORM image, sRGB view)
- [ ] At frame end, release the image to `VK_QUEUE_FAMILY_EXTERNAL` in GENERAL layout; wait the fence; D3D11 copies it into a `CreateSwapChainForComposition` swapchain on a DirectComposition visual over the HWND
- [ ] Premultiplied alpha for transparent windows; `DXGI_ALPHA_MODE_IGNORE` for opaque ones
- [ ] A D3D event query so the next frame doesn't overwrite the texture mid-copy
- [ ] A frame-latency waitable swapchain (latency 1) instead of a blocking present
- [ ] A background layer in the window's clear color (a 1×1 surface scaled to cover); a late frame stays anchored top-left instead of stretching
- [ ] Use it for **every** window on Windows; `ELG_PRESENT=vulkan` restores Vulkan swapchains for opaque windows
- [ ] If the interop is missing, fall back to opaque with a warning

## Done when

- [ ] A transparent window with a rounded, shadowed panel shows the desktop at its corners
- [ ] A custom title bar drags, double-click maximizes, and Aero Snap works, while buttons inside it still click
- [ ] Fast edge-dragging never shows a stretched frame

## Gotchas

- Windows Vulkan drivers only expose opaque composite alpha; on the development machine's AMD GPU,
  `SDL_WINDOW_TRANSPARENT` alone gave black corners.
- Follow-ups: shared fences (keyed mutex or `VK_KHR_external_semaphore_win32`) instead of CPU waits
  around the copy; the app's own window icon; jump lists.
