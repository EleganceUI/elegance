---
title: Microphone and camera
description: "Recording with miniaudio, and cameras through Media Foundation or DirectShow, delivering CPU frames to the GPU converter."
sidebar:
  order: 4
depends: [media/audio, media/video]
---

## What it's for

Voice notes, calls and video previews need the microphone and camera. Capture reuses the existing
pieces: miniaudio for input, and the video path's `cpu` frames for camera pictures, so a camera
preview is a `VideoSource` like any video.

## How it works

### AudioRecorder

`AudioRecorder({.device_id, .sample_rate, .channels, .on_samples})` opens a miniaudio **capture**
device (`ma_device_type_capture`, float samples):

- The capture callback appends each block to an in-memory buffer, tracks the recent **peak**, and
  calls `on_samples` (on the audio thread) for apps that stream instead.
- A ticker posts `duration` and `level` to the UI thread about 15 times a second while recording.
- `start`, `stop`, `toggle`, `clear`; reactive `is_recording`, `duration`, `level`, `error`.
  Access denied by Windows privacy settings surfaces as `error`, not an exception.
- Results: `samples()`, `wav()` and `save_wav(path)` (16-bit PCM with a standard RIFF header), and
  `sound()`, which wraps them as a `Sound` for playback.
- `audio_input_devices()` lists inputs. An id is the device's miniaudio id in hex.

The callback currently takes a mutex to append. A lock-free ring would make it follow the audio
rule strictly.

### Camera

`Camera({.device_id, .width, .height, .mirrored})` is a `VideoSource`; `CameraView(camera, mode)`
shows it. A **capture thread** produces `cpu` frames.

**Media Foundation first.** A source reader on the device asks for **NV12** (RGB32 if the camera
can't). Among the camera's native modes it picks the one closest to the requested size,
preferring larger, then the highest frame rate. Each sample becomes a packed, top-down frame.

**DirectShow as a fallback.** Some cameras (virtual cameras on Windows 10, such as OBS Virtual
Camera and Snap Camera) are invisible to Media Foundation. For those, a filter graph runs:
camera → Sample Grabber → Null Renderer.

- `IAMStreamConfig` picks the closest format.
- Frames are taken in the camera's **own** format (RGB32, NV12, YUY2, I420). Forcing RGB32 made
  DirectShow insert converters that OBS's camera never fed.
- The graph runs **without a reference clock**. Otherwise the Null Renderer holds each frame until
  its presentation time and stalls the stream after the first one.
- Bottom-up RGB32 is flipped to top-down.
- The Sample Grabber's interfaces are declared in Elegance, because `qedit.h` left the Windows SDK
  while `qedit.dll` still ships.

`camera_devices()` lists Media Foundation's cameras plus DirectShow-only ones (ids prefixed
`dshow:`).

**Display.** Frames go to the renderer as `cpu` frames and are converted on the GPU (see
[Video](../video/)), which cost 0.17 cores for 1080p60 in release versus 1.25 converting on the CPU.
The preview is **mirrored** by default (uv read right to left), since people expect to see
themselves as in a mirror.

**Snapshots.** `snapshot()` converts the newest frame on the CPU (BT.709 or BT.601, limited
range), unmirrored, and encodes PNG through WIC.

## Rules

- Capture threads never touch UI state; everything goes through UI tasks.
- Cameras deliver `cpu` frames, so every platform's camera reuses the same GPU path.
- Permission problems are reactive errors, not exceptions.

## Edge cases

- **Matrix and range** are guessed from height today; they should come from the media type.
- **Camera controls** (focus, exposure) and mode lists aren't exposed.
- **macOS and Linux** need AVCaptureSession and PipeWire/V4L2 backends producing `cpu` frames.
