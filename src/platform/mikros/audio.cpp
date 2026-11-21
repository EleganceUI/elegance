// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Audio on Mikros, which has no audio output or input: the public types of elg/audio.h and
// elg/capture.h, in place of media/audio.cpp, audio_stream.cpp and recorder.cpp (miniaudio, which
// isn't built there). There are no devices, and playing a sound or recording fails with the error
// of platform/mikros/unavailable.h. What can't be reached without an object that playing or
// recording made has the values of one that never played.

#include <elg/audio.h>
#include <elg/capture.h>

#include "platform/mikros/unavailable.h"

#include <algorithm>
#include <memory>

namespace elg::detail {

// What an AudioRecorder reports: the format asked for, clamped as other systems clamp it. This is
// media/recorder.cpp's RecorderState, which a build that has that file doesn't have this one with.
struct RecorderState {
    std::uint32_t sample_rate = 0;
    std::uint32_t channels = 0;
};

namespace {

[[noreturn]] void no_audio_output() {
    platform::mikros::unavailable("audio output");
}

[[noreturn]] void no_audio_input() {
    platform::mikros::unavailable("audio recording");
}

} // namespace

} // namespace elg::detail

namespace elg {

std::vector<AudioDevice> audio_output_devices() {
    return {};
}

std::vector<AudioDevice> audio_input_devices() {
    return {};
}

// --- Sound ---

Sound Sound::load(const std::filesystem::path&) {
    detail::no_audio_output();
}

Sound Sound::load(std::span<const unsigned char>) {
    detail::no_audio_output();
}

void Sound::play(PlayOptions) const {
    detail::no_audio_output();
}

double Sound::duration() const noexcept {
    return 0;
}

// --- AudioPlayer: none can be made ---

AudioPlayer::AudioPlayer(const std::filesystem::path&) {
    detail::no_audio_output();
}

void AudioPlayer::play() const {
    detail::no_audio_output();
}

void AudioPlayer::pause() const {}

void AudioPlayer::toggle() const {
    detail::no_audio_output();
}

void AudioPlayer::stop() const {}

void AudioPlayer::seek(double) const {}

void AudioPlayer::set_volume(float) const {}

void AudioPlayer::set_looping(bool) const {}

bool AudioPlayer::is_playing() const {
    return false;
}

double AudioPlayer::time() const {
    return 0;
}

double AudioPlayer::duration() const {
    return 0;
}

float AudioPlayer::volume() const {
    return 1;
}

bool AudioPlayer::is_looping() const {
    return false;
}

Binding<float> AudioPlayer::time_binding() const {
    detail::no_audio_output();
}

Binding<float> AudioPlayer::volume_binding() const {
    detail::no_audio_output();
}

// --- AudioStream: none can be made ---

AudioStream::AudioStream(AudioStreamOptions) {
    detail::no_audio_output();
}

void AudioStream::write(std::span<const float>) const {}

void AudioStream::clear() const {}

std::size_t AudioStream::queued_frames() const {
    return 0;
}

void AudioStream::set_volume(float) const {}

void AudioStream::start() const {
    detail::no_audio_output();
}

void AudioStream::stop() const {}

std::string AudioStream::error() const {
    return {};
}

std::uint32_t AudioStream::sample_rate() const noexcept {
    return 0;
}

std::uint32_t AudioStream::channels() const noexcept {
    return 0;
}

// --- AudioRecorder: made, but it can't record ---

AudioRecorder::AudioRecorder(AudioRecorderOptions options)
    : state_(std::make_shared<detail::RecorderState>(std::clamp<std::uint32_t>(options.sample_rate, 8000, 192000),
                                                     std::clamp<std::uint32_t>(options.channels, 1, 2))) {}

void AudioRecorder::start() const {
    detail::no_audio_input();
}

void AudioRecorder::stop() const {}

void AudioRecorder::toggle() const {
    detail::no_audio_input();
}

void AudioRecorder::clear() const {}

bool AudioRecorder::is_recording() const {
    return false;
}

double AudioRecorder::duration() const {
    return 0;
}

float AudioRecorder::level() const {
    return 0;
}

std::string AudioRecorder::error() const {
    return detail::platform::mikros::unavailable_message("audio recording");
}

std::uint32_t AudioRecorder::sample_rate() const noexcept {
    return state_->sample_rate;
}

std::uint32_t AudioRecorder::channels() const noexcept {
    return state_->channels;
}

std::vector<float> AudioRecorder::samples() const {
    return {};
}

void AudioRecorder::save_wav(const std::filesystem::path&) const {
    detail::no_audio_input();
}

std::vector<unsigned char> AudioRecorder::wav() const {
    detail::no_audio_input();
}

Sound AudioRecorder::sound() const {
    detail::no_audio_input();
}

} // namespace elg
