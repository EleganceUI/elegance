---
title: Windows, title bars, and DirectComposition
description: "Window options, custom title bars and caption buttons, and why every Windows window presents through DirectComposition."
sidebar:
  order: 4
depends: [controls/basic-controls, renderer/vulkan]
---

## What it's for

Modern apps draw their own title bars, sometimes have transparent or rounded windows, and must
resize smoothly. This part covers the window options, the hit testing that keeps a custom title
bar draggable, the caption buttons, and the presentation path that makes transparency and
stretch-free resizing possible on Windows.

## How it works

### Window options

`WindowDesc` holds the title, size, minimum size, background, `title_bar` (`TitleBar::hidden`
removes the system frame), `transparent` and `opacity`. Borderless windows on Windows 11 keep
rounded corners through the DWM corner preference, unless they're transparent (the app draws its
own shape then).

`WindowHandle`, from `Environment<WindowKey>()`, lets views act on their window: `close()` (a
close request, like the system button), `minimize()`, `toggle_maximized()`, and tracked
`is_maximized()` / `is_active()` fed by window events. The state behind a handle outlives the
window, so a kept handle just goes inert.

### Custom title bars

An undecorated window installs a hit-test function, which SDL calls during event processing:

1. Within **6 pt of an edge** (unless maximized or fixed-size): the matching resize edge.
2. Otherwise, ask the dispatcher about the **innermost handler under the point**. A
   `.window_drag_area()` marker means *drag*; anything else (a button, a field) means *normal*.

Returning *drag* hands the gesture to the OS, which gives native double-click-to-maximize and Aero
Snap. Buttons inside the bar still work, because they're the innermost handler where they are.

`WindowControls({.style, .minimize, .maximize})` draws caption buttons: Windows style (46×32,
red hover on close, Segoe MDL2 glyphs or drawn fallbacks) or macOS traffic lights. Both dim when
the window is inactive (`is_active()`), and both are `.focusable(false)` so Tab skips them and
clicks don't steal focus.

### Presentation on Windows: DirectComposition

Vulkan swapchains on Windows are opaque, and a swapchain image is **stretched** to the window
when the window is resized faster than frames arrive. Both problems go away if Elegence presents
through DirectComposition (DComp), the compositor API under modern Windows UI.

Per window, `CompositionTarget` builds:

1. A **D3D11 device** on the same GPU as Vulkan, matched by the adapter LUID the render thread
   publishes.
2. A **shared texture**, created by D3D11 with an NT handle and imported into Vulkan with
   `VK_KHR_external_memory_win32`: a UNORM image with an sRGB view to render through.
3. A **composition swapchain** (`CreateSwapChainForComposition`): premultiplied alpha for
   transparent windows, `DXGI_ALPHA_MODE_IGNORE` for opaque ones, and a frame-latency waitable
   object (latency 1).
4. A **DComp visual tree** on the HWND: a background visual in the window's clear color (a 1×1
   surface scaled to cover the window) under the content visual showing the swapchain.

Each frame:

1. Wait (with a timeout) on the waitable object, so frames are paced by the display instead of a
   blocking present.
2. Render into the shared image with Vulkan.
3. At the end of the command buffer, transition it to `GENERAL` and **release it to
   `VK_QUEUE_FAMILY_EXTERNAL`**, then wait the fence.
4. D3D11 copies the shared texture into the swapchain's back buffer; an **event query** keeps the
   next frame from overwriting the texture before the copy finishes.
5. Present with **sync interval 0** (DWM synchronizes anyway; interval 1 added a frame of latency).

The swapchain is shown at its own pixel size, never stretched. While a resize outruns the frames,
the newest frame stays anchored top-left over the background color.

Every window uses this path. `ELG_PRESENT=vulkan` switches opaque windows back to a plain Vulkan
swapchain. If the interop isn't available, a transparent window falls back to opaque with a
warning.

## Rules

- Never stretch a frame; show it at its size over the background.
- Drag areas are opt-in; everything else in a title bar behaves like a normal view.
- Caption buttons never take focus.

## Edge cases

- **CPU waits around the copy**: the fence wait and event query cost a little latency. Shared
  fences (a keyed mutex or `VK_KHR_external_semaphore_win32`) would let the GPUs synchronize
  directly.
- **Maximized borderless windows** have no resize edges.
- **Other platforms** use premultiplied composite alpha on the Vulkan swapchain where the surface
  offers it.
