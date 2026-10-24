---
title: macOS and Linux
description: "Where each platform plugs in, which interfaces already isolate it, and what each backend has to provide."
sidebar:
  order: 1
depends: [system/accessibility, media/capture, media/networking, text/fallback-and-emoji, controls/popups-and-menus]
---

:::caution[Design, not built]
Most of this is planned. The interfaces it relies on exist; the non-Windows backends mostly
don't.
:::

## What it's for

Elegance targets Windows, Linux (X11 and Wayland) and macOS. The code is Windows-first, but
almost every platform-specific piece already sits behind an internal interface. Porting means
writing backends, not restructuring.

## How it works

### The seams that already exist

| Seam | Interface | Windows today | macOS | Linux |
|---|---|---|---|---|
| Windows, input, clipboard, dialogs, tray | `platform::` (SDL3) | SDL3 | SDL3 | SDL3 (X11 and Wayland) |
| GPU | Vulkan 1.3 | Native | MoltenVK + portability extension | Native |
| Presentation | `WindowTarget` | DirectComposition | Vulkan swapchain (premultiplied alpha where offered) | Vulkan swapchain |
| Images | `image_decode` | WIC | ImageIO | stb_image / libspng |
| Video playback | `VideoSource` | Media Foundation | AVFoundation (`AVPlayerItemVideoOutput`) | GStreamer `playbin` + `appsink` |
| Video frames | `VideoFrame` kinds | `d3d11_shared`, `cpu` | IOSurface import, or `cpu` | DMA-BUF import, or `cpu` |
| Camera | `Camera` (a `VideoSource`) | MF + DirectShow | AVCaptureSession → `cpu` | PipeWire / V4L2 → `cpu` |
| Audio | miniaudio | WASAPI | Core Audio | PulseAudio / ALSA |
| HTTP | `http::` | WinHTTP | NSURLSession | libcurl |
| Accessibility | `AccessibilityAdapter` | AccessKit (UIA) | AccessKit (NSAccessibility) | AccessKit (AT-SPI) |
| Fonts | `FontLibrary` | Fallback chain by name | CoreText discovery | fontconfig |
| Notifications | `notify` | Shell icon | `UNUserNotificationCenter` | D-Bus notifications |

### The `cpu` frame as the universal path

Any platform that can produce packed BGRA, NV12, YUY2 or I420 bytes gets video and camera display
for free: the renderer uploads and converts them on the GPU. Zero-copy imports (IOSurface,
DMA-BUF) are optimizations to add after a backend works with `cpu` frames.

### Platform-specific behavior

- **Menus**: macOS opens context menus on **press**, and apps expect a native menu bar
  (`App::menu_bar(...)` as `NSMenu`). Elsewhere a menu bar is an ordinary view.
- **Keys**: `primary_modifier()` is Cmd on macOS; word movement uses Alt there.
- **Window chrome**: traffic lights on macOS (`WindowControls` already draws them).
- **Paths**: the glyph cache and prefs follow each platform's conventions.
- **Hot reload**: `dlopen` by unique path; `dlclose` may not unload on macOS.

## Rules

- No platform `#ifdef` outside the backend files.
- A new backend first targets the simplest path (`cpu` frames, Vulkan swapchains), then optimizes.

## Edge cases

- **Wayland** has no global window positions. Popup placement must go through SDL's
  `xdg_popup` support, never absolute coordinates.
- **MoltenVK** lacks some formats and features; check the stencil formats and external memory
  paths there first.
