---
title: Audio
description: "Playing sound effects and music, and controlling playback from the UI."
---

Elegence plays audio through miniaudio. Sound goes to the system's default output device and
follows it when it changes (headphones plugged in, for example). Supported formats are **WAV**,
**FLAC** and **MP3**.

## Sound effects

A `Sound` is decoded into memory once. Every `play()` starts a new voice, so plays can overlap:

```cpp
const Sound click = Sound::load("assets/click.wav");

Button("Tap", [=] { click.play(); });
Button("Quiet tap", [=] { click.play({.volume = 0.4f, .pitch = 1.2f, .pan = -0.5f}); });
```

`Sound::load` also takes encoded audio in memory, and throws `elg::ResourceError` if the sound
can't be read. Copies of a `Sound` share the decoded data.

## Music and long audio

An `AudioPlayer` decodes as it plays, so long files start immediately and use little memory:

```cpp
const AudioPlayer music("assets/theme.mp3");

Button(Text([=] { return music.is_playing() ? "Pause" : "Play"; }), [=] { music.toggle(); });
Slider(music.time_binding(), {.max = static_cast<float>(music.duration())});
Slider(music.volume_binding());
Text([=] { return std::format("{:.0f} / {:.0f} s", music.time(), music.duration()); });
```

The controls are `play`, `pause`, `toggle`, `stop` (which also rewinds), `seek(seconds)`,
`set_volume` and `set_looping`. The readers (`is_playing`, `time`, `duration`, `volume`,
`is_looping`) are reactive, so views showing them update while it plays.

`time_binding()` follows the playback position and seeks when written, which is what a scrubber
slider needs. Copies of a player control the same playback; it stops when the last copy goes.

## Threads and devices

`Sound` and `AudioPlayer` can be used from any thread; their reactive readers are for the UI
thread, like any state. Without an output device, playing does nothing and says so once in the
log.

To record audio, see [Microphone and camera](../capture/).
