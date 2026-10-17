// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Live audio out (AudioStream) and the list of outputs, on miniaudio's playback devices.

#include <elg/audio.h>

#include "core/log.h"

#include <elg/reactive.h>

#include <miniaudio.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <deque>
#include <format>
#include <mutex>
#include <optional>

namespace elg::detail {

namespace {

std::string device_hex(const void* data, std::size_t size) {
    std::string out;
    const auto* bytes = static_cast<const unsigned char*>(data);
    for (std::size_t i = 0; i < size; ++i) {
        out += std::format("{:02x}", bytes[i]);
    }
    return out;
}

std::optional<ma_device_id> find_output(ma_context& context, const std::string& id) {
    ma_device_info* infos = nullptr;
    ma_uint32 count = 0;
    if (ma_context_get_devices(&context, &infos, &count, nullptr, nullptr) != MA_SUCCESS) {
        return std::nullopt;
    }
    for (ma_uint32 i = 0; i < count; ++i) {
        if (device_hex(&infos[i].id, sizeof(ma_device_id)) == id) {
            return infos[i].id;
        }
    }
    return std::nullopt;
}

} // namespace

struct AudioStreamState {
    AudioStreamOptions options;
    ma_context context{};
    ma_device device{};
    bool context_ready = false;
    bool device_ready = false;

    std::mutex mutex;
    std::deque<float> queue; // interleaved
    std::atomic<float> volume{1.f};

    State<std::string> error{std::string()};

    ~AudioStreamState() {
        if (device_ready) {
            ma_device_uninit(&device);
        }
        if (context_ready) {
            ma_context_uninit(&context);
        }
    }

    std::size_t max_queued() const { return static_cast<std::size_t>(options.sample_rate) * options.channels; }

    // The audio thread.
    static void on_data(ma_device* device, void* output, const void*, ma_uint32 frames) {
        auto* self = static_cast<AudioStreamState*>(device->pUserData);
        auto* out = static_cast<float*>(output);
        const std::size_t count = static_cast<std::size_t>(frames) * self->options.channels;
        std::fill(out, out + count, 0.f);
        if (self->options.on_render) {
            self->options.on_render(std::span(out, count));
        } else {
            std::lock_guard lock(self->mutex);
            const std::size_t take = std::min(count, self->queue.size());
            std::copy_n(self->queue.begin(), take, out);
            self->queue.erase(self->queue.begin(), self->queue.begin() + static_cast<std::ptrdiff_t>(take));
        }
        const float volume = self->volume.load();
        if (volume != 1.f) {
            for (std::size_t i = 0; i < count; ++i) {
                out[i] *= volume;
            }
        }
    }

    bool open() {
        if (ma_context_init(nullptr, 0, nullptr, &context) != MA_SUCCESS) {
            error = std::string("audio output isn't available");
            return false;
        }
        context_ready = true;
        ma_device_config config = ma_device_config_init(ma_device_type_playback);
        config.playback.format = ma_format_f32;
        config.playback.channels = options.channels;
        config.sampleRate = options.sample_rate;
        config.dataCallback = on_data;
        config.pUserData = this;
        std::optional<ma_device_id> id;
        if (!options.device_id.empty()) {
            id = find_output(context, options.device_id);
            if (!id) {
                log_warning("AudioStream: output {} isn't connected; using the default", options.device_id);
            } else {
                config.playback.pDeviceID = &*id;
            }
        }
        if (ma_device_init(&context, &config, &device) != MA_SUCCESS) {
            error = std::string("can't open the audio output");
            log_warning("AudioStream: {}", error.peek());
            return false;
        }
        device_ready = true;
        return true;
    }
};

} // namespace elg::detail

namespace elg {

std::vector<AudioDevice> audio_output_devices() {
    std::vector<AudioDevice> devices;
    ma_context context;
    if (ma_context_init(nullptr, 0, nullptr, &context) != MA_SUCCESS) {
        return devices;
    }
    ma_device_info* infos = nullptr;
    ma_uint32 count = 0;
    if (ma_context_get_devices(&context, &infos, &count, nullptr, nullptr) == MA_SUCCESS) {
        for (ma_uint32 i = 0; i < count; ++i) {
            devices.push_back({detail::device_hex(&infos[i].id, sizeof(ma_device_id)), infos[i].name, infos[i].isDefault != 0});
        }
    }
    ma_context_uninit(&context);
    return devices;
}

AudioStream::AudioStream(AudioStreamOptions options) : state_(std::make_shared<detail::AudioStreamState>()) {
    options.sample_rate = std::clamp<std::uint32_t>(options.sample_rate, 8000, 192000);
    options.channels = std::clamp<std::uint32_t>(options.channels, 1, 2);
    state_->options = std::move(options);
    if (state_->open() && ma_device_start(&state_->device) != MA_SUCCESS) {
        state_->error = std::string("can't start the audio output");
    }
}

void AudioStream::write(std::span<const float> samples) const {
    std::lock_guard lock(state_->mutex);
    state_->queue.insert(state_->queue.end(), samples.begin(), samples.end());
    const std::size_t limit = state_->max_queued();
    if (state_->queue.size() > limit) {
        // Whole frames, so channels stay in step.
        std::size_t drop = state_->queue.size() - limit;
        drop -= drop % state_->options.channels;
        state_->queue.erase(state_->queue.begin(), state_->queue.begin() + static_cast<std::ptrdiff_t>(drop));
    }
}

void AudioStream::clear() const {
    std::lock_guard lock(state_->mutex);
    state_->queue.clear();
}

std::size_t AudioStream::queued_frames() const {
    std::lock_guard lock(state_->mutex);
    return state_->queue.size() / state_->options.channels;
}

void AudioStream::set_volume(float volume) const {
    state_->volume = std::max(0.f, volume);
}

void AudioStream::start() const {
    if (state_->device_ready) {
        ma_device_start(&state_->device);
    }
}

void AudioStream::stop() const {
    if (state_->device_ready) {
        ma_device_stop(&state_->device);
    }
}

std::string AudioStream::error() const {
    return state_->error();
}

std::uint32_t AudioStream::sample_rate() const noexcept {
    return state_->options.sample_rate;
}

std::uint32_t AudioStream::channels() const noexcept {
    return state_->options.channels;
}

} // namespace elg
