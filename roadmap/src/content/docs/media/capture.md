---
title: Microphone and camera
description: AudioRecorder on miniaudio, Camera on Media Foundation with a DirectShow fallback.
sidebar:
  order: 4
---

Capture reuses everything before it: miniaudio for input, and the `cpu` video frame path for
camera frames, which the renderer converts on the GPU.

:::note[Depends on]
[Audio](../audio/) and [Video](../video/) (the `VideoSource` handoff and `video_convert` pass).
:::

## Read first

- `design.md` M5 "Media part 5, capture"
- `include/elg/capture.h`, `src/media/recorder.cpp`, `src/media/camera.cpp`, `tests/capture_tests.cpp`

## Tasks

### AudioRecorder
- [ ] `AudioRecorder({.device_id, .sample_rate, .channels, .on_samples})` with miniaudio capture (f32) into memory
- [ ] `start`, `stop`, `toggle`, `clear`; reactive `is_recording`, `duration`, `level` (recent peak, ~15×/s while recording), `error` (e.g. access denied in privacy settings)
- [ ] `samples()`, `wav()`, `save_wav()` (16-bit PCM), `sound()` for playback
- [ ] `on_samples` streams blocks on the audio thread
- [ ] `audio_input_devices()` (id = the device id in hex)

### Camera
- [ ] `Camera({.device_id, .width, .height, .mirrored})` as a `VideoSource`; `CameraView(camera, mode)`
- [ ] Media Foundation source reader on a capture thread, asking for NV12 (RGB32 if not); pick the native mode closest to the requested size (larger preferred), then the best frame rate
- [ ] DirectShow fallback for cameras Media Foundation doesn't see (virtual cameras on Windows 10): camera → Sample Grabber → Null Renderer; `IAMStreamConfig` picks the closest format
- [ ] Take DirectShow frames in the camera's own format (RGB32, NV12, YUY2, I420); flip bottom-up RGB32
- [ ] Run the graph without a reference clock
- [ ] Mirrored preview by default (read uv right to left)
- [ ] `snapshot()`: the newest frame, unmirrored, as PNG through WIC (a CPU YUV converter remains for this)
- [ ] `camera_devices()`: Media Foundation's cameras plus DirectShow-only ones (ids prefixed `dshow:`)

## Done when

- [ ] Recording from the default mic shows a moving level meter, then plays back
- [ ] OBS Virtual Camera (DirectShow, NV12 1080p60) shows live in `CameraView`
- [ ] Release-build CPU for a 1080p60 preview is around the design's 0.17 cores

## Gotchas

- **Forcing RGB32 in DirectShow** made it insert converters that OBS's camera never fed.
- **The Null Renderer held frames until their presentation time** and stalled the stream after
  the first frame. Run without a reference clock.
- `qedit.h` left the Windows SDK while `qedit.dll` still ships the Sample Grabber. Declare its
  interfaces yourself.
- CPU conversion took 1.25 cores. The GPU pass took 0.17. An earlier D3D11 video processor took
  0.11 but was Windows-only, which is why it was replaced.
- Follow-ups: macOS (AVCaptureSession) and Linux (PipeWire/V4L2); capturing straight into the
  upload buffer; per-slot fences for many windows showing one source; reading matrix and range
  from the media type; camera controls; a lock-free recorder buffer (the callback takes a mutex
  today); recording video through Media Foundation's sink writer.
