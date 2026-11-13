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
//   Button(Text([=] { return music.is_playing() ? "Pause" : "Play"; }), [=] { music.toggle(); })
//   Slider(music.time_binding(), {.max = static_cast<float>(music.duration())})
//
// Formats: WAV, FLAC, and MP3. Sound goes to the system's default output device and follows it
// when it changes (headphones plugged in). Without an output device, playing does nothing (and
// says so once in the log). Thread-safe; the reactive reads (is_playing, time, ...) are for the
// UI thread, like any state.

#include <elg/export.h>
#include <elg/reactive.h>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace elg {

namespace detail {
struct SoundData;
struct AudioPlayerState;
struct AudioStreamState;
} // namespace detail

/// An audio input or output device, as listed by audio_output_devices() or audio_input_devices().
struct AudioDevice {
    std::string id;          ///< for AudioRecorderOptions / AudioStreamOptions::device_id
    std::string name;        ///< as the system shows it
    bool is_default = false; ///< Whether it's the system's default device.
};

/// The system's audio outputs (speakers, headphones, virtual devices).
///
/// @return The outputs; empty when the system has no audio.
ELG_API std::vector<AudioDevice> audio_output_devices();

/// How Sound::play() plays a sound.
struct PlayOptions {
    float volume = 1; ///< 0...1 (and beyond, amplified); negative counts as 0
    float pitch = 1;  ///< playback speed and pitch: 2 is an octave up (at least 0.01)
    float pan = 0;    ///< -1 left ... 1 right
};

/// A short sound, decoded into memory once; every play() is a new voice, so plays overlap.
/// Copies share the sound.
class ELG_API Sound {
public:
    /// Decodes a sound file (WAV, FLAC or MP3) into memory.
    ///
    /// @param file The file.
    /// @return The sound.
    /// @throws ResourceError If the file can't be read or decoded.
    static Sound load(const std::filesystem::path& file);
    /// Decodes encoded audio in memory (copied).
    ///
    /// @param data The encoded audio (WAV, FLAC or MP3).
    /// @return The sound.
    /// @throws ResourceError If the data can't be decoded.
    static Sound load(std::span<const unsigned char> data);

    /// Plays the sound from the start in a new voice, over any that are still playing. Returns at
    /// once; does nothing without an output device.
    ///
    /// @param options Volume, pitch and pan.
    void play(PlayOptions options = {}) const;
    /// @return The sound's length in seconds.
    [[nodiscard]] double duration() const noexcept;

private:
    explicit Sound(std::shared_ptr<const detail::SoundData> data) noexcept;
    ELG_PRIVATE_MEMBERS_BEGIN
    std::shared_ptr<const detail::SoundData> data_;
    ELG_PRIVATE_MEMBERS_END
};

/// Music, speech, anything long: decoded while it plays. Copies control the same player; it
/// stops when the last copy goes. Without an output device, it's a silent player that never plays.
class ELG_API AudioPlayer {
public:
    /// Opens `file` for streaming, paused at the start.
    ///
    /// @param file A WAV, FLAC or MP3 file.
    /// @throws ResourceError If the file can't be opened or isn't audio.
    explicit AudioPlayer(const std::filesystem::path& file);

    /// Starts or resumes playing; from the start again if it had played to the end.
    void play() const;
    /// Pauses, keeping the position.
    void pause() const;
    /// Pauses while playing, plays otherwise.
    void toggle() const;
    /// Pauses and goes back to the start.
    void stop() const;
    /// Moves the playback position.
    ///
    /// @param seconds Seconds from the start, clamped to [0, duration()].
    void seek(double seconds) const;
    /// @param volume 0...1 (and beyond, amplified); negative counts as 0.
    void set_volume(float volume) const;
    /// @param looping Whether playing past the end starts over.
    void set_looping(bool looping) const;

    // Reactive (tracked reads): views showing them update while it plays.

    /// @return Whether it's playing (tracked).
    [[nodiscard]] bool is_playing() const;
    /// @return The playback position in seconds from the start (tracked; updated about 15 times a
    ///   second while playing).
    [[nodiscard]] double time() const;
    /// @return The length in seconds (tracked); 0 without an output device.
    [[nodiscard]] double duration() const;
    /// @return The volume set with set_volume() (tracked); 1 at first.
    [[nodiscard]] float volume() const;
    /// @return Whether it loops (tracked).
    [[nodiscard]] bool is_looping() const;

    /// For a Slider: reading follows playback, writing seeks.
    ///
    /// @return A binding to the position in seconds, from 0 to duration().
    [[nodiscard]] Binding<float> time_binding() const;
    /// For a Slider: reading follows the volume, writing sets it.
    ///
    /// @return A binding to the volume.
    [[nodiscard]] Binding<float> volume_binding() const;

private:
    ELG_PRIVATE_MEMBERS_BEGIN
    std::shared_ptr<detail::AudioPlayerState> state_;
    ELG_PRIVATE_MEMBERS_END
};

/// Live audio out (voice, synthesized sound): samples written as they're made, played as soon as
/// the device takes them.
///
/// @code
/// const AudioStream out({.channels = 2});
/// out.write(decoded);                      // from any thread; queued
/// @endcode
///
/// or mixed on demand, on the audio thread:
///
/// @code
/// AudioStream({.on_render = [=](std::span<float> buffer) { mixer.fill(buffer); }})
/// @endcode
struct AudioStreamOptions {
    /// empty: the system's default output (followed when it changes). An id that isn't connected
    /// falls back to the default, with a warning in the log.
    std::string device_id;
    std::uint32_t sample_rate = 48000; ///< Frames per second, clamped to 8000...192000.
    std::uint32_t channels = 2;        ///< 1 (mono) or 2 (stereo); other counts are clamped.
    /// Fills each buffer the device asks for (interleaved, -1...1, zeroed beforehand) instead of
    /// playing what write() queued. Runs on the audio thread: keep it short and lock-light. If it
    /// throws, the stream goes silent and the exception is rethrown on the UI thread.
    std::function<void(std::span<float>)> on_render;
};

/// Live audio out (see AudioStreamOptions). Copies share the stream; it stops when the last copy
/// goes.
class ELG_API AudioStream {
public:
    /// Opens the output and starts playing. Failing to doesn't throw: error() says why.
    ///
    /// @param options The device, format, and an optional render callback.
    explicit AudioStream(AudioStreamOptions options = {});

    /// Queues interleaved samples (without on_render). Any thread. Beyond about a second queued,
    /// the oldest are dropped, so a stalled device doesn't build up delay.
    ///
    /// @param samples Interleaved samples, -1...1, a whole number of frames.
    void write(std::span<const float> samples) const;
    /// Drops what's queued.
    void clear() const;
    /// @return How many frames write() has queued that the device hasn't taken yet.
    [[nodiscard]] std::size_t queued_frames() const;
    /// @param volume 0...1 (and beyond, amplified); negative counts as 0.
    void set_volume(float volume) const;
    /// Resumes playing after stop(). It starts playing when made.
    void start() const;
    /// Pauses playing; what's queued stays queued.
    void stop() const;

    /// @return Reactive: why it can't play (no device, failed to open); empty when fine.
    [[nodiscard]] std::string error() const;

    /// @return The sample rate in use, after clamping.
    [[nodiscard]] std::uint32_t sample_rate() const noexcept;
    /// @return The channel count in use, after clamping.
    [[nodiscard]] std::uint32_t channels() const noexcept;

private:
    ELG_PRIVATE_MEMBERS_BEGIN
    std::shared_ptr<detail::AudioStreamState> state_;
    ELG_PRIVATE_MEMBERS_END
};

} // namespace elg

#endif // ELG_AUDIO_H
