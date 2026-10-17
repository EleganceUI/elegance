---
title: Video
description: Media Foundation playback into shared textures, the VideoSource frame handoff, GPU YUV conversion, and VideoFeed.
sidebar:
  order: 3
---

Decoding stays on the OS media stack, and frames stay on the GPU. The render thread redraws
playing video by itself, so no UI-thread work happens per video frame.

:::note[Depends on]
[Raster images](../../graphics/images/) (texture path), [Windows and DirectComposition](../../controls/windows/)
(D3D11/Vulkan interop), and [Audio](../audio/).
:::

## Read first

- `design.md` §13.3 part 3 (and 3a, the FFmpeg backend), M5 "Media part 3", "Frame handoff"
- `include/elg/video.h`, `src/media/video_source.*`, `src/media/video_feed.cpp` (newer), `src/gpu/video_textures.*`, `shaders/video_convert.frag`, `src/view/video_view.cpp`

## Tasks

### Player API
- [ ] `VideoPlayer(path or URL)`: `play`, `pause`, `toggle`, `stop`, `seek`, `set_volume`, `set_muted`, `set_looping`
- [ ] Reactive `is_playing`, `is_ready`, `time`, `duration`, `natural_size`, `volume`, `is_muted`, `is_looping`, `error`
- [ ] `progress_binding()` (0...1, writing seeks) and `volume_binding()`
- [ ] Keep requests made before the decoder exists and apply them when it starts
- [ ] `VideoView(player, ContentMode)`: fitted or filled (clipped) on black; ideal size is the video's (320×180 until known)
- [ ] `VideoControls(player)`: play/pause, scrubber, time and mute, as ordinary views

### Windows backend
- [ ] `IMFMediaEngine` in frame-server mode on a D3D11 device on the renderer's adapter (by the LUID the render thread publishes)
- [ ] Engine events (metadata, time update, play, pause, end, error) reach UI state through `post_ui_task`; the callback never takes the source's lock
- [ ] `TransferVideoFrame` into a ring of 3 shared BGRA D3D11 textures (NT handles), waiting for the copy

### Frame handoff and GPU
- [ ] `VideoSource` interface; a tagged `VideoFrame`: `d3d11_shared` (shared texture), or `cpu` (packed BGRA, NV12, YUY2 or I420, with matrix BT.601/709 and range)
- [ ] `VideoCommand` in the display list; a view emits it even before the size is known (the render thread seeing it starts the decoder)
- [ ] `VideoTextures`: import each shared texture once and sample it in place; each command buffer acquires from `VK_QUEUE_FAMILY_EXTERNAL` and releases at its end
- [ ] `cpu` frames: upload into a host-visible storage buffer, then a fullscreen pass (`video_convert.frag`) writes BGRA through a UNORM view; sample through an sRGB view
- [ ] A ring of three images and buffers per source; recreate on size change; free after 240 idle frames
- [ ] The render thread keeps each video window's last frame and polls every 4 ms while playing, redrawing when there's a new frame

### VideoFeed (newer)
- [ ] `VideoFeed::push(format, width, height, pixels, full_range)` from any thread (BGRA, NV12, I420; packed, top-down); `clear()`
- [ ] Reactive `size()`, `has_frame()`; copies share the feed
- [ ] `VideoFeedView(feed, mode, mirrored)`

## Done when

- [ ] A 640×360 30 fps H.264 clip plays with sound in sync, muted and looping, and also filled into a circle clip
- [ ] Closing mid-playback exits cleanly with no validation errors
- [ ] CPU while playing is in the range the design measured (about 9% of one core, debug with validation)

## Gotchas

- Doing the YUV→RGB math through an sRGB view keeps it in gamma space, as the standards expect.
- macOS and Linux are a stub ("not available yet"). AVFoundation and GStreamer are next, delivering
  `cpu` frames or IOSurface / DMA-BUF imports.
- Not built yet: the optional FFmpeg backend (LGPL, dynamic, royalty-free decoders by default),
  GPU-side sync instead of CPU waits, HDR/10-bit, subtitles, track selection, fullscreen, and
  vsync-based pacing instead of 4 ms polling.
