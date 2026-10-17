---
title: macOS and Linux
description: Everything that is Windows-only today.
sidebar:
  order: 1
---

The design targets Windows, Linux (X11 and Wayland) and macOS (through MoltenVK). Today most
platform-specific pieces exist only for Windows.

:::note[Depends on]
The Windows version of each subsystem, as a reference for the shape of the interface.
:::

## Tasks

### Build and renderer
- [ ] `x64-linux` and `arm64-osx` triplets building in CI
- [ ] MoltenVK with the portability extension; premultiplied composite alpha where offered
- [ ] GCC needs `-lstdc++exp` for `std::stacktrace`

### Text
- [ ] System font discovery: CoreText (macOS), fontconfig (Linux)
- [ ] Fallback chains for macOS and Linux

### Media and networking
- [ ] Images: ImageIO (macOS); stb_image or libspng elsewhere
- [ ] Video: AVFoundation (`AVPlayer` + `AVPlayerItemVideoOutput`, IOSurface → MoltenVK) and GStreamer (`playbin` + `appsink`, DMA-BUF when available)
- [ ] Camera: AVCaptureSession and PipeWire/V4L2, delivering `cpu` frames
- [ ] HTTP: NSURLSession (macOS), libcurl (Linux); libcurl's WebSocket API

### System
- [ ] Accessibility: AccessKit's NSAccessibility and AT-SPI adapters
- [ ] `App::menu_bar(...)` as a native `NSMenu`
- [ ] Context menus open on press on macOS; Ctrl-click and long-press openers
- [ ] Notifications and tray equivalents
- [ ] Glyph cache paths: `~/Library/Caches/Elegence/GlyphCache` and `$XDG_CACHE_HOME/elegence/glyphs`

## Done when

- [ ] Every example runs on macOS and on Linux under both X11 and Wayland
