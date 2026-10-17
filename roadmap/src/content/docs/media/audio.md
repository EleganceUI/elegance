---
title: Audio
description: miniaudio, sound effects, streamed players with reactive state, and raw audio streams.
sidebar:
  order: 2
---

One miniaudio engine per process. Short effects decode once and overlap; long files stream.
Player state is reactive, so a scrubber is just a Slider on a binding.

:::note[Depends on]
[Reactive core](../../framework/reactive/) and `post_ui_task`.
:::

## Read first

- `design.md` §13.3 part 2, M5 "Media part 2"
- `include/elg/audio.h`, `src/media/audio.cpp`, `src/media/miniaudio.cpp`, `src/media/audio_stream.cpp` (newer), `tests/audio_tests.cpp`

## Tasks

### Engine
- [ ] Compile miniaudio once (`media/miniaudio.cpp`); one `ma_engine`, started on first use, following the default output device
- [ ] No device: log once and play silently
- [ ] `audio_output_devices()` listing

### Sound
- [ ] `Sound::load(file | data)`: decode once to the engine's format (so playing never resamples)
- [ ] `play({.volume, .pitch, .pan})` starts a new voice; plays overlap
- [ ] Sweep finished voices on the next play, never on the audio thread

### AudioPlayer
- [ ] Streamed (`MA_SOUND_FLAG_STREAM`): `play`, `pause`, `toggle`, `stop`, `seek`, `set_volume`, `set_looping`
- [ ] Reactive `is_playing()`, `time()`, `duration()`, `volume()`, `is_looping()`; `time_binding()` (scrubbing seeks) and `volume_binding()`
- [ ] Copies share one player, which stops with the last copy
- [ ] A ticker thread posts the position about 15×/s, only while something plays, plus one final update at the end

### AudioStream (newer)
- [ ] `AudioStream({.device_id, .sample_rate, .channels, .on_render})`; copies share it
- [ ] `write(samples)` from any thread queues interleaved float samples; beyond about a second queued, drop the oldest so a stalled device doesn't build delay
- [ ] Or `on_render(span<float>)` fills each buffer on the audio thread (zeroed first)
- [ ] `clear`, `queued_frames`, `set_volume`, `start`, `stop`; reactive `error()`

## Done when

- [ ] Rapid clicks overlap with pitch and pan; a music player scrubs and loops
- [ ] Unplugging headphones moves playback to the speakers
- [ ] Tests: a WAV built in memory has the right duration; missing and garbage inputs throw `ResourceError`
- [ ] Idle CPU is 0 when nothing plays

## Gotchas

- The audio callback must never allocate or lock. Pass commands through a lock-free queue.
- Formats today: WAV, FLAC, MP3. Not built yet: Ogg Vorbis (stb_vorbis), AAC/M4A through the
  platform decoders.
