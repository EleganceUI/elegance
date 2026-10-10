// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Microphone recording (design.md §13.3) on miniaudio's capture devices.

#include <elg/capture.h>

#include "core/log.h"
#include "core/ui_tasks.h"

#include <elg/error.h>

#include <miniaudio.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <format>
#include <fstream>
#include <mutex>

namespace elg::detail {

namespace {

std::string hex(const void* data, std::size_t size) {
    std::string out;
    const auto* bytes = static_cast<const unsigned char*>(data);
    for (std::size_t i = 0; i < size; ++i) {
        out += std::format("{:02x}", bytes[i]);
    }
    return out;
}

// The device whose id (hex of its ma_device_id) is `id`.
std::optional<ma_device_id> find_input(ma_context& context, const std::string& id) {
    ma_device_info* infos = nullptr;
    ma_uint32 count = 0;
    if (ma_context_get_devices(&context, nullptr, nullptr, &infos, &count) != MA_SUCCESS) {
        return std::nullopt;
    }
    for (ma_uint32 i = 0; i < count; ++i) {
        if (hex(&infos[i].id, sizeof(ma_device_id)) == id) {
            return infos[i].id;
        }
    }
    return std::nullopt;
}

} // namespace

struct RecorderState {
    AudioRecorderOptions options;
    ma_context context{};
    ma_device device{};
    bool context_ready = false;
    bool device_ready = false;

    std::mutex mutex;
    std::vector<float> samples; // interleaved

    std::atomic<float> peak{0};
    std::atomic<double> last_post{0};
    std::weak_ptr<RecorderState> self;

    State<bool> recording{false};
    State<double> duration{0.0};
    State<float> level{0.f};
    State<std::string> error{std::string()};

    ~RecorderState() {
        if (device_ready) {
            ma_device_uninit(&device);
        }
        if (context_ready) {
            ma_context_uninit(&context);
        }
    }

    double seconds() {
        std::lock_guard lock(mutex);
        return static_cast<double>(samples.size()) / options.channels / options.sample_rate;
    }

    // The audio thread.
    static void on_data(ma_device* device, void*, const void* input, ma_uint32 frames) {
        auto* self = static_cast<RecorderState*>(device->pUserData);
        if (!input || frames == 0) {
            return;
        }
        const auto* data = static_cast<const float*>(input);
        const std::size_t count = static_cast<std::size_t>(frames) * self->options.channels;
        float peak = 0;
        for (std::size_t i = 0; i < count; ++i) {
            peak = std::max(peak, std::abs(data[i]));
        }
        {
            std::lock_guard lock(self->mutex);
            self->samples.insert(self->samples.end(), data, data + count);
        }
        if (self->options.on_samples) {
            self->options.on_samples(std::span(data, count));
        }
        // The meter and the duration, about 15 times a second.
        self->peak = std::max(self->peak.load(), peak);
        const double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        if (now - self->last_post.load() >= 0.066) {
            self->last_post = now;
            const float level = self->peak.exchange(0);
            post_ui_task([weak = self->self, level] {
                if (const auto state = weak.lock()) {
                    state->level = std::min(level, 1.f);
                    state->duration = state->seconds();
                }
            });
        }
    }

    bool open() {
        if (device_ready) {
            return true;
        }
        if (!context_ready) {
            if (ma_context_init(nullptr, 0, nullptr, &context) != MA_SUCCESS) {
                error = std::string("audio input isn't available");
                return false;
            }
            context_ready = true;
        }
        ma_device_config config = ma_device_config_init(ma_device_type_capture);
        config.capture.format = ma_format_f32;
        config.capture.channels = options.channels;
        config.sampleRate = options.sample_rate;
        config.dataCallback = on_data;
        config.pUserData = this;
        std::optional<ma_device_id> id;
        if (!options.device_id.empty()) {
            id = find_input(context, options.device_id);
            if (!id) {
                error = std::string("that microphone isn't connected");
                return false;
            }
            config.capture.pDeviceID = &*id;
        }
        if (ma_device_init(&context, &config, &device) != MA_SUCCESS) {
            error = std::string("can't open the microphone (is access allowed in Settings > Privacy?)");
            log_warning("AudioRecorder: {}", error.peek());
            return false;
        }
        device_ready = true;
        return true;
    }
};

namespace {

std::vector<unsigned char> encode_wav(const std::vector<float>& samples, std::uint32_t rate, std::uint32_t channels) {
    std::vector<unsigned char> out;
    const auto put = [&](std::uint32_t value, int bytes) {
        for (int i = 0; i < bytes; ++i) {
            out.push_back(static_cast<unsigned char>(value >> (8 * i)));
        }
    };
    const auto tag = [&](const char* text) { out.insert(out.end(), text, text + 4); };
    const auto data_size = static_cast<std::uint32_t>(samples.size() * 2);
    tag("RIFF");
    put(36 + data_size, 4);
    tag("WAVE");
    tag("fmt ");
    put(16, 4);
    put(1, 2); // PCM
    put(channels, 2);
    put(rate, 4);
    put(rate * channels * 2, 4);
    put(channels * 2, 2);
    put(16, 2);
    tag("data");
    put(data_size, 4);
    out.reserve(out.size() + data_size);
    for (const float sample : samples) {
        const auto value = static_cast<std::int16_t>(std::lround(std::clamp(sample, -1.f, 1.f) * 32767));
        put(static_cast<std::uint16_t>(value), 2);
    }
    return out;
}

} // namespace

} // namespace elg::detail

namespace elg {

std::vector<AudioDevice> audio_input_devices() {
    std::vector<AudioDevice> devices;
    ma_context context;
    if (ma_context_init(nullptr, 0, nullptr, &context) != MA_SUCCESS) {
        return devices;
    }
    ma_device_info* infos = nullptr;
    ma_uint32 count = 0;
    if (ma_context_get_devices(&context, nullptr, nullptr, &infos, &count) == MA_SUCCESS) {
        for (ma_uint32 i = 0; i < count; ++i) {
            devices.push_back({detail::hex(&infos[i].id, sizeof(ma_device_id)), infos[i].name, infos[i].isDefault != 0});
        }
    }
    ma_context_uninit(&context);
    return devices;
}

AudioRecorder::AudioRecorder(AudioRecorderOptions options) : state_(std::make_shared<detail::RecorderState>()) {
    options.sample_rate = std::clamp<std::uint32_t>(options.sample_rate, 8000, 192000);
    options.channels = std::clamp<std::uint32_t>(options.channels, 1, 2);
    state_->options = std::move(options);
    state_->self = state_;
}

void AudioRecorder::start() const {
    if (state_->recording.peek() || !state_->open()) {
        return;
    }
    if (ma_device_start(&state_->device) != MA_SUCCESS) {
        state_->error = std::string("can't start the microphone");
        return;
    }
    state_->error = std::string();
    state_->recording = true;
}

void AudioRecorder::stop() const {
    if (!state_->recording.peek()) {
        return;
    }
    ma_device_stop(&state_->device);
    state_->recording = false;
    state_->level = 0.f;
    state_->duration = state_->seconds();
}

void AudioRecorder::toggle() const {
    state_->recording.peek() ? stop() : start();
}

void AudioRecorder::clear() const {
    {
        std::lock_guard lock(state_->mutex);
        state_->samples.clear();
    }
    state_->duration = 0.0;
}

bool AudioRecorder::is_recording() const {
    return state_->recording();
}

double AudioRecorder::duration() const {
    return state_->duration();
}

float AudioRecorder::level() const {
    return state_->level();
}

std::string AudioRecorder::error() const {
    return state_->error();
}

std::uint32_t AudioRecorder::sample_rate() const noexcept {
    return state_->options.sample_rate;
}

std::uint32_t AudioRecorder::channels() const noexcept {
    return state_->options.channels;
}

std::vector<float> AudioRecorder::samples() const {
    std::lock_guard lock(state_->mutex);
    return state_->samples;
}

std::vector<unsigned char> AudioRecorder::wav() const {
    return detail::encode_wav(samples(), sample_rate(), channels());
}

void AudioRecorder::save_wav(const std::filesystem::path& file) const {
    const std::vector<unsigned char> bytes = wav();
    std::ofstream out(file, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!out) {
        throw ResourceError(std::format("can't write \"{}\"", file.string()));
    }
}

Sound AudioRecorder::sound() const {
    return Sound::load(wav());
}

} // namespace elg
