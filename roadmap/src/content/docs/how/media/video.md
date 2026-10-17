---
title: Video
description: "The VideoSource interface, Media Foundation playback into shared textures, CPU frames converted on the GPU, and redrawing without layout."
sidebar:
  order: 3
depends: [graphics/images, controls/windows, media/audio, controls/basic-controls]
---

## What it's for

Apps play video: intros, tutorials, media libraries, call windows. Decoding should use the OS's
hardware decoders and codec licenses. Frames should stay on the GPU. A playing video should cost
no UI-thread work per frame.

## How it works

### Three layers

| Layer | Role |
|---|---|
| `VideoPlayer` (public) | A handle with methods and reactive state; copies share one `VideoSource` |
| `VideoSource` (internal) | A platform backend: decodes, plays sound, keeps sync, hands frames to the renderer |
| `VideoTextures` (render thread) | Turns a source's current frame into something the UI shader samples |

`VideoView(player, mode)` paints a `VideoCommand` referencing the source, fitted or filled (and
clipped) on black. Its ideal size is the video's, or 320×180 until known. `VideoControls(player)`
is ordinary views (play/pause Button, a Slider on `progress_binding()`, time Text, mute) built
on the reactive state.

### VideoSource

The interface has two sides:

- **UI thread**: `play`, `pause`, `seek`, `set_volume`, `set_muted`, `set_looping`, and `State`s
  for playing, ready, time, duration, size, volume, muted, looping and error.
- **Render thread**: `update()` takes a new frame from the decoder if one is due (true if the
  current frame changed); `current()` returns the current `VideoFrame`; `is_playing_now()` is an
  atomic flag saying frames keep coming.

A `VideoFrame` is **tagged**, so every platform can hand frames over the same way:

- `d3d11_shared`: a BGRA D3D11 texture with an NT handle, sampled in place by Vulkan;
- `cpu`: packed pixels in BGRA, NV12, YUY2 or I420, with a YUV matrix (BT.601/709) and range.

### Windows backend

`IMFMediaEngine` in **frame-server mode** does demuxing, hardware decoding, audio output and A/V
sync:

- It runs on a D3D11 device created on the **renderer's adapter**, whose LUID the render thread
  publishes once the device exists. The engine is therefore created lazily. Requests made before
  it exists (play, seek, volume) are **remembered and applied** when it starts.
- Engine events (metadata loaded, time update, play, pause, ended, error) arrive on Media
  Foundation's threads. The callback holds the source **weakly** and posts UI tasks that update
  the states. It **never takes the source's mutex**, because the engine may call back while a
  UI-thread call into the engine holds it.
- On `update()`, if the engine has a new frame, `TransferVideoFrame` writes it into the next of a
  **ring of 3 shared textures** (so frames still in flight keep theirs), then waits for the copy.

### Frames on the GPU

`d3d11_shared` frames: `VideoTextures` imports each shared texture **once**
(`VK_KHR_external_memory_win32`, sRGB view) and samples it in place. Each command buffer
**acquires** the texture from `VK_QUEUE_FAMILY_EXTERNAL` (old layout `GENERAL`, since the decoder
defined its contents) and releases it at the end.

`cpu` frames: each source gets a ring of three **host-visible storage buffers** and BGRA images.
For a new frame, copy the bytes into the next buffer, then run a fullscreen pass
(`video_convert.frag`) that reads the bytes as the frame's format and writes BGRA through a UNORM
view. The image is sampled through an **sRGB** view, so the YUV→RGB math stays in gamma space as
the standards define. The conversion runs once per new frame, before the window passes, however
many windows show the source. Rings are recreated on size change (after a device wait, rare) and
freed after 240 idle frames.

### Redrawing without layout

The render thread keeps the **last frame** of every window that showed a video. While any source
`is_playing_now()`, it wakes every **4 ms**, calls `update()` on each, and redraws the windows
whose video changed by replaying their kept frame. The UI thread does nothing per video frame.

A `VideoView` emits its `VideoCommand` even before the size is known, because the render thread
seeing the source is what starts the decoder.

### VideoFeed

`VideoFeed` is a source the **app** feeds: `push(format, width, height, pixels, full_range)` from
any thread (BGRA, NV12, I420; packed, top-down), `clear()`, and reactive `size()` /
`has_frame()`. It produces `cpu` frames, converted like the camera's. `VideoFeedView(feed, mode,
mirrored)` shows it, which covers decoders the app brings itself, remote video, and generated
frames.

## Rules

- Decoders share the renderer's GPU.
- No UI-thread work per video frame.
- The engine's callbacks never take the source's lock.
- Every platform delivers frames as one of the tagged kinds.

## Edge cases

- **Other platforms** report "not available yet" through `error()`. AVFoundation and GStreamer
  are planned, delivering `cpu` frames or importing IOSurfaces / DMA-BUFs.
- **4 ms polling** is a simple substitute for display-synced pacing.
- **Many windows showing one cpu source** could overwrite a ring slot still in flight; per-slot
  fences would fix it.
