// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_AUDIO_H
#define ELG_AUDIO_H

// Audio playback (design.md §13.3).
//
//   const Sound click = Sound::load("assets/click.wav");    // short effects: decoded once
//   Button("Tap", [=] { click.play({.volume = 0.6f}); })
//
//   const AudioPlayer music("assets/theme.mp3");            // long files: streamed as they play
//   Button([=] { return music.is_playing() ? "Pause" : "Play"; }, [=] { music.toggle(); })
//   Slider(music.time_binding(), {.max = static_cast<float>(music.duration())})
//
// Formats: WAV, FLAC, and MP3. Sound goes to the system's default output device and follows it
// when it changes (headphones plugged in). Without an output device, playing does nothing (and
// says so once in the log). Thread-safe; the reactive reads (is_playing, time, ...) are for the
// UI thread, like any state.

#include <elg/export.h>
#include <elg/reactive.h>

#include <filesystem>
#include <memory>
#include <span>

namespace elg {

namespace detail {
struct SoundData;
struct AudioPlayerState;
} // namespace detail

struct PlayOptions {
    float volume = 1; // 0...1 (and beyond, amplified)
    float pitch = 1;  // playback speed and pitch: 2 is an octave up
    float pan = 0;    // -1 left ... 1 right
};

// A short sound, decoded into memory once; every play() is a new voice, so plays overlap.
// Copies share the sound.
class ELG_API Sound {
public:
    // Throws elg::ResourceError if the file can't be read or decoded.
    static Sound load(const std::filesystem::path& file);
    // Encoded audio in memory (copied).
    static Sound load(std::span<const unsigned char> data);

    void play(PlayOptions options = {}) const;
    // Seconds.
    double duration() const noexcept;

private:
    explicit Sound(std::shared_ptr<const detail::SoundData> data) noexcept;
    ELG_PRIVATE_MEMBERS_BEGIN
    std::shared_ptr<const detail::SoundData> data_;
    ELG_PRIVATE_MEMBERS_END
};

// Music, speech, anything long: decoded while it plays. Copies control the same player; it
// stops when the last copy goes.
class ELG_API AudioPlayer {
public:
    // Throws elg::ResourceError if the file can't be opened or isn't audio.
    explicit AudioPlayer(const std::filesystem::path& file);

    void play() const;
    void pause() const;
    void toggle() const;
    void stop() const; // pauses and goes back to the start
    void seek(double seconds) const;
    void set_volume(float volume) const;
    void set_looping(bool looping) const;

    // Reactive (tracked reads): views showing them update while it plays.
    bool is_playing() const;
    double time() const; // seconds from the start
    double duration() const;
    float volume() const;
    bool is_looping() const;

    // For a Slider: reading follows playback, writing seeks.
    Binding<float> time_binding() const;
    Binding<float> volume_binding() const;

private:
    ELG_PRIVATE_MEMBERS_BEGIN
    std::shared_ptr<detail::AudioPlayerState> state_;
    ELG_PRIVATE_MEMBERS_END
};

} // namespace elg

#endif // ELG_AUDIO_H
