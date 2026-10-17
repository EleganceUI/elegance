---
title: Audio
description: "One miniaudio engine, decoded sound effects, streamed players with reactive state, and raw output streams."
sidebar:
  order: 2
depends: [framework/reactive]
---

## What it's for

Apps play short sounds (clicks, notifications), longer media (music, voice), and sometimes audio
they generate or receive (synths, calls). Playback must never stutter because the UI is busy, and
player state (playing, position, volume) must be reactive so controls are plain views.

## How it works

### The engine

One miniaudio **`ma_engine`** per process, started on first use. It opens the default output
device and **follows it** when it changes (headphones plugged in). It mixes all voices on
miniaudio's device thread. With no output device, it logs once and every play is silent; nothing
throws. miniaudio is header-only and compiled once in `media/miniaudio.cpp`.

The audio thread has one rule: **it never allocates or takes a lock**. Everything it touches is
prepared beforehand on other threads.

### Sound: short effects

`Sound::load(file | data)` **decodes the whole file up front** into the engine's sample format and
rate, so playing never resamples. A `Sound` is a shared handle to that buffer.

`play({.volume, .pitch, .pan})` starts a **new voice** (an `ma_sound` with its own cursor over the
shared buffer), so plays overlap: rapid clicks don't cut each other off. Finished voices are
swept on the **next** `play` call, on the calling thread, never on the audio thread.

### AudioPlayer: long media

`AudioPlayer(file)` **streams** (`MA_SOUND_FLAG_STREAM`): it decodes as it plays. Methods:
`play`, `pause`, `toggle`, `stop`, `seek`, `set_volume`, `set_looping`. Reactive reads:
`is_playing()`, `time()`, `duration()`, `volume()`, `is_looping()`. Bindings `time_binding()`
(writing seeks, for a scrubber Slider) and `volume_binding()`.

Position updates come from a **ticker thread**:

- while any player plays, it wakes about 15 times a second and posts one UI task that reads each
  playing player's position (and whether it ended) into its states;
- when nothing plays, it sleeps until a `play()` wakes it, so idle CPU is 0;
- after playback ends it posts one final update, so the states settle at "stopped, at the end".

Copies of a player share it, and playback stops when the last copy goes.

Formats: WAV, FLAC and MP3 (miniaudio's decoders).

### AudioStream: raw output

`AudioStream({.device_id, .sample_rate, .channels, .on_render})` opens its own playback device:

- **Push mode**: `write(samples)` from any thread appends interleaved float samples to a queue
  the audio callback drains. Beyond about a second queued, the **oldest** samples are dropped, so a
  stalled device doesn't build up delay. Writes are whole frames, so channels stay in step.
- **Pull mode**: with `on_render`, the callback calls it with a zeroed buffer to fill instead.
  It runs on the audio thread, so it must be short and lock-light.
- `clear()`, `queued_frames()`, `set_volume()`, `start()`/`stop()`, and a reactive `error()`
  (no device, failed to open).

`audio_output_devices()` lists outputs for `device_id`.

## Rules

- The audio callback never allocates or locks.
- Sound effects never resample at play time.
- Player state reaches views through States, updated on the UI thread.
- No CPU while nothing plays.

## Edge cases

- **Device changed**: the engine follows the default device. What a stream opened on a
  specific device should do when that device disappears is worth defining (report it through
  `error()`).
- **Seeking past the end** clamps to the duration.
- **Missing or garbage files** throw `ResourceError` at load.
