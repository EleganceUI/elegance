// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Audio playback (design.md §13.3) on miniaudio's engine: one per process, started on first
// use, mixing on miniaudio's device thread.

#include <elg/audio.h>

#include "core/log.h"
#include "core/ui_tasks.h"

#include <elg/error.h>

#include <miniaudio.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <format>
#include <fstream>
#include <list>
#include <mutex>
#include <thread>
#include <vector>

namespace elg::detail {

namespace {

// The process's engine, started on first use; nullptr when there's no output device.
class AudioEngine {
public:
    static AudioEngine& instance() {
        static AudioEngine engine;
        return engine;
    }

    ma_engine* get() {
        std::call_once(started_, [this] {
            if (ma_engine_init(nullptr, &engine_) == MA_SUCCESS) {
                ready_ = true;
                log_debug("audio: {} Hz, {} channels", ma_engine_get_sample_rate(&engine_),
                          ma_engine_get_channels(&engine_));
            } else {
                log_warning("audio: no output device; sounds won't play");
            }
        });
        return ready_ ? &engine_ : nullptr;
    }

    ~AudioEngine() {
        if (ready_) {
            ma_engine_uninit(&engine_);
        }
    }

private:
    std::once_flag started_;
    ma_engine engine_{};
    bool ready_ = false;
};

std::vector<unsigned char> read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) {
        throw ResourceError(std::format("can't read \"{}\"", path.string()));
    }
    const std::streamsize size = in.tellg();
    std::vector<unsigned char> bytes(static_cast<std::size_t>(std::max<std::streamsize>(size, 0)));
    in.seekg(0);
    in.read(reinterpret_cast<char*>(bytes.data()), size);
    return bytes;
}

} // namespace

// --- Sound ---

struct SoundData {
    std::vector<float> frames; // interleaved, at the engine's rate and channel count
    ma_uint32 channels = 0;
    ma_uint32 sample_rate = 0;
    ma_uint64 frame_count = 0;

    // Voices playing (or finished, until swept). Each owns its buffer cursor.
    struct Voice {
        ma_audio_buffer_ref buffer{};
        ma_sound sound{};
    };
    mutable std::mutex mutex;
    mutable std::list<Voice> voices;

    ~SoundData() {
        for (Voice& voice : voices) {
            ma_sound_uninit(&voice.sound);
            ma_audio_buffer_ref_uninit(&voice.buffer);
        }
    }
};

namespace {

std::shared_ptr<const SoundData> decode_sound(std::span<const unsigned char> bytes, const std::string& name) {
    auto data = std::make_shared<SoundData>();
    ma_engine* engine = AudioEngine::instance().get();
    // Decoded to the engine's format up front, so playing never resamples.
    const ma_uint32 channels = engine ? ma_engine_get_channels(engine) : 2;
    const ma_uint32 rate = engine ? ma_engine_get_sample_rate(engine) : 48000;
    ma_decoder_config config = ma_decoder_config_init(ma_format_f32, channels, rate);
    void* frames = nullptr;
    ma_uint64 count = 0;
    if (ma_decode_memory(bytes.data(), bytes.size(), &config, &count, &frames) != MA_SUCCESS) {
        throw ResourceError(std::format("{} isn't audio this build can decode (WAV, FLAC, MP3)", name));
    }
    data->channels = channels;
    data->sample_rate = rate;
    data->frame_count = count;
    const float* samples = static_cast<const float*>(frames);
    data->frames.assign(samples, samples + count * channels);
    ma_free(frames, nullptr);
    return data;
}

} // namespace

} // namespace elg::detail

namespace elg {

Sound::Sound(std::shared_ptr<const detail::SoundData> data) noexcept : data_(std::move(data)) {}

Sound Sound::load(const std::filesystem::path& file) {
    return Sound(detail::decode_sound(detail::read_file(file), file.filename().string()));
}

Sound Sound::load(std::span<const unsigned char> data) {
    return Sound(detail::decode_sound(data, "the audio data"));
}

double Sound::duration() const noexcept {
    return data_ && data_->sample_rate ? static_cast<double>(data_->frame_count) / data_->sample_rate : 0;
}

void Sound::play(PlayOptions options) const {
    ma_engine* engine = detail::AudioEngine::instance().get();
    if (!engine || !data_ || data_->frame_count == 0) {
        return;
    }
    std::lock_guard lock(data_->mutex);
    // Finished voices are swept here, on a calling thread (never the audio thread).
    auto& voices = data_->voices;
    for (auto it = voices.begin(); it != voices.end();) {
        if (ma_sound_at_end(&it->sound)) {
            ma_sound_uninit(&it->sound);
            ma_audio_buffer_ref_uninit(&it->buffer);
            it = voices.erase(it);
        } else {
            ++it;
        }
    }
    auto& voice = voices.emplace_back();
    if (ma_audio_buffer_ref_init(ma_format_f32, data_->channels, data_->frames.data(), data_->frame_count,
                                 &voice.buffer) != MA_SUCCESS) {
        voices.pop_back();
        return;
    }
    if (ma_sound_init_from_data_source(engine, &voice.buffer, MA_SOUND_FLAG_NO_SPATIALIZATION, nullptr,
                                       &voice.sound) != MA_SUCCESS) {
        ma_audio_buffer_ref_uninit(&voice.buffer);
        voices.pop_back();
        return;
    }
    ma_sound_set_volume(&voice.sound, std::max(options.volume, 0.f));
    ma_sound_set_pitch(&voice.sound, std::max(options.pitch, 0.01f));
    ma_sound_set_pan(&voice.sound, std::clamp(options.pan, -1.f, 1.f));
    ma_sound_start(&voice.sound);
}

} // namespace elg

namespace elg::detail {

// --- AudioPlayer ---

struct AudioPlayerState {
    ma_sound sound{};
    bool ready = false;
    std::string name;
    // What views read; written on the UI thread (by the player's methods and the ticker).
    State<bool> playing{false};
    State<double> time{0.0};
    State<double> duration{0.0};
    State<float> volume{1.f};
    State<bool> looping{false};

    ~AudioPlayerState() {
        if (ready) {
            ma_sound_uninit(&sound);
        }
    }

    // UI thread: reads the playback position and end into the states.
    void refresh() {
        if (!ready) {
            return;
        }
        float seconds = 0;
        ma_sound_get_cursor_in_seconds(&sound, &seconds);
        time = static_cast<double>(seconds);
        const bool now = ma_sound_is_playing(&sound) != 0;
        if (playing.peek() != now) {
            playing = now;
        }
    }
};

namespace {

// Posts position updates to the UI thread ~15 times a second while any player plays.
class AudioTicker {
public:
    static AudioTicker& instance() {
        static AudioTicker ticker;
        return ticker;
    }

    void watch(const std::shared_ptr<AudioPlayerState>& player) {
        {
            std::lock_guard lock(mutex_);
            std::erase_if(players_, [](const auto& weak) { return weak.expired(); });
            players_.push_back(player);
            if (!thread_.joinable()) {
                thread_ = std::jthread([this](std::stop_token stop) { run(stop); });
            }
        }
        wake_.notify_all();
    }

    ~AudioTicker() {
        thread_.request_stop();
        wake_.notify_all();
    }

private:
    void run(std::stop_token stop) {
        bool was_playing = false;
        while (!stop.stop_requested()) {
            std::vector<std::weak_ptr<AudioPlayerState>> playing;
            {
                std::unique_lock lock(mutex_);
                const auto any_playing = [&] {
                    playing.clear();
                    for (const auto& weak : players_) {
                        if (const auto player = weak.lock(); player && player->ready && ma_sound_is_playing(&player->sound)) {
                            playing.push_back(weak);
                        }
                    }
                    return !playing.empty();
                };
                // Idle while nothing plays: woken by play().
                wake_.wait_for(lock, stop, std::chrono::milliseconds(500), any_playing);
            }
            if (stop.stop_requested()) {
                return;
            }
            if (playing.empty() && !was_playing) {
                continue; // idle: nothing to update, so the UI thread isn't woken
            }
            was_playing = !playing.empty();
            // One more update after playback ends, so the states settle (stopped, at the end).
            std::vector<std::weak_ptr<AudioPlayerState>> all;
            {
                std::lock_guard lock(mutex_);
                all = players_;
            }
            post_ui_task([all] {
                for (const auto& weak : all) {
                    if (const auto player = weak.lock()) {
                        player->refresh();
                    }
                }
            });
            if (!playing.empty()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(66));
            }
        }
    }

    std::mutex mutex_;
    std::condition_variable_any wake_;
    std::vector<std::weak_ptr<AudioPlayerState>> players_;
    std::jthread thread_;
};

} // namespace

} // namespace elg::detail

namespace elg {

AudioPlayer::AudioPlayer(const std::filesystem::path& file) : state_(std::make_shared<detail::AudioPlayerState>()) {
    state_->name = file.filename().string();
    if (!std::filesystem::exists(file)) {
        throw ResourceError(std::format("can't open \"{}\"", file.string()));
    }
    ma_engine* engine = detail::AudioEngine::instance().get();
    if (!engine) {
        return; // no output device: a silent player
    }
    const ma_uint32 flags = MA_SOUND_FLAG_STREAM | MA_SOUND_FLAG_NO_SPATIALIZATION;
#if defined(_WIN32)
    const ma_result result = ma_sound_init_from_file_w(engine, file.c_str(), flags, nullptr, nullptr, &state_->sound);
#else
    const ma_result result = ma_sound_init_from_file(engine, file.c_str(), flags, nullptr, nullptr, &state_->sound);
#endif
    if (result != MA_SUCCESS) {
        throw ResourceError(std::format("{} isn't audio this build can play (WAV, FLAC, MP3)", state_->name));
    }
    state_->ready = true;
    float length = 0;
    ma_sound_get_length_in_seconds(&state_->sound, &length);
    state_->duration = static_cast<double>(length);
    detail::AudioTicker::instance().watch(state_);
}

void AudioPlayer::play() const {
    if (state_->ready) {
        if (ma_sound_at_end(&state_->sound)) {
            ma_sound_seek_to_pcm_frame(&state_->sound, 0); // played to the end: start over
        }
        ma_sound_start(&state_->sound);
        detail::AudioTicker::instance().watch(state_);
    }
    state_->playing = state_->ready;
}

void AudioPlayer::pause() const {
    if (state_->ready) {
        ma_sound_stop(&state_->sound);
        state_->refresh();
    }
    state_->playing = false;
}

void AudioPlayer::toggle() const {
    is_playing() ? pause() : play();
}

void AudioPlayer::stop() const {
    pause();
    seek(0);
}

void AudioPlayer::seek(double seconds) const {
    if (!state_->ready) {
        return;
    }
    const double clamped = std::clamp(seconds, 0.0, state_->duration.peek());
    ma_uint32 rate = 0;
    ma_sound_get_data_format(&state_->sound, nullptr, nullptr, &rate, nullptr, 0);
    ma_sound_seek_to_pcm_frame(&state_->sound, static_cast<ma_uint64>(clamped * rate));
    state_->time = clamped;
}

void AudioPlayer::set_volume(float volume) const {
    volume = std::max(volume, 0.f);
    if (state_->ready) {
        ma_sound_set_volume(&state_->sound, volume);
    }
    state_->volume = volume;
}

void AudioPlayer::set_looping(bool looping) const {
    if (state_->ready) {
        ma_sound_set_looping(&state_->sound, looping);
    }
    state_->looping = looping;
}

bool AudioPlayer::is_playing() const {
    return state_->playing();
}

double AudioPlayer::time() const {
    return state_->time();
}

double AudioPlayer::duration() const {
    return state_->duration();
}

float AudioPlayer::volume() const {
    return state_->volume();
}

bool AudioPlayer::is_looping() const {
    return state_->looping();
}

Binding<float> AudioPlayer::time_binding() const {
    const AudioPlayer self = *this;
    return Binding<float>([self] { return static_cast<float>(self.time()); }, [self](float t) { self.seek(static_cast<double>(t)); });
}

Binding<float> AudioPlayer::volume_binding() const {
    const AudioPlayer self = *this;
    return Binding<float>([self] { return self.volume(); }, [self](float v) { self.set_volume(v); });
}

} // namespace elg
