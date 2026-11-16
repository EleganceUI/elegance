// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Cameras (design.md §13.3). Each OS captures in platform/<os>/camera.*: Media Foundation (or
// DirectShow) on Windows, AVFoundation on macOS, V4L2 on Linux. Frames stay in the camera's format
// (NV12, YUY2, I420 or BGRA) and go to the renderer as VideoFrame::Kind::cpu, which converts them
// on the GPU, the same on every platform.

#include <elg/capture.h>

#include "core/ui_tasks.h"
#include "media/camera_source.h"
#include "media/image_codec_backend.h"
#include "media/source_host.h"
#include "media/video_backend.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace elg::detail {

std::vector<std::uint8_t> frame_to_bgra(VideoFrame::Format format, VideoFrame::Matrix matrix, bool full_range,
                                        std::span<const std::uint8_t> pixels, std::uint32_t width,
                                        std::uint32_t height) {
    const std::size_t count = static_cast<std::size_t>(width) * height;
    std::vector<std::uint8_t> out(count * 4, 255);
    if (pixels.size() < packed_size(format, width, height)) {
        return {};
    }
    if (format == VideoFrame::Format::bgra || format == VideoFrame::Format::rgba) {
        const bool swap = format == VideoFrame::Format::rgba;
        for (std::size_t i = 0; i < count; ++i) {
            out[i * 4 + 0] = pixels[i * 4 + (swap ? 2 : 0)];
            out[i * 4 + 1] = pixels[i * 4 + 1];
            out[i * 4 + 2] = pixels[i * 4 + (swap ? 0 : 2)];
        }
        return out;
    }
    // Y'CbCr -> R'G'B' (Kr, Kb per matrix), video or full range.
    const float kr = matrix == VideoFrame::Matrix::bt709 ? 0.2126f : 0.299f;
    const float kb = matrix == VideoFrame::Matrix::bt709 ? 0.0722f : 0.114f;
    const float kg = 1 - kr - kb;
    const float y_scale = full_range ? 1.f / 255 : 1.f / 219;
    const float c_scale = full_range ? 1.f / 255 : 1.f / 224;
    const float y_offset = full_range ? 0.f : 16.f;
    const std::uint32_t chroma_w = (width + 1) / 2;
    const std::uint32_t chroma_h = (height + 1) / 2;
    const auto store = [&](std::size_t i, int y, int cb, int cr) {
        const float luma = (static_cast<float>(y) - y_offset) * y_scale;
        const float pb = (static_cast<float>(cb) - 128) * c_scale;
        const float pr = (static_cast<float>(cr) - 128) * c_scale;
        const float r = luma + 2 * (1 - kr) * pr;
        const float b = luma + 2 * (1 - kb) * pb;
        const float g = (luma - kr * r - kb * b) / kg;
        const auto byte = [](float v) { return static_cast<std::uint8_t>(std::lround(std::clamp(v, 0.f, 1.f) * 255)); };
        out[i * 4 + 0] = byte(b);
        out[i * 4 + 1] = byte(g);
        out[i * 4 + 2] = byte(r);
    };
    for (std::uint32_t row = 0; row < height; ++row) {
        for (std::uint32_t col = 0; col < width; ++col) {
            const std::size_t i = static_cast<std::size_t>(row) * width + col;
            switch (format) {
            case VideoFrame::Format::nv12: {
                const std::size_t c = count + (static_cast<std::size_t>(row / 2) * chroma_w + col / 2) * 2;
                store(i, pixels[i], pixels[c], pixels[c + 1]);
                break;
            }
            case VideoFrame::Format::i420: {
                const std::size_t c = static_cast<std::size_t>(row / 2) * chroma_w + col / 2;
                const std::size_t plane = static_cast<std::size_t>(chroma_w) * chroma_h;
                store(i, pixels[i], pixels[count + c], pixels[count + plane + c]);
                break;
            }
            case VideoFrame::Format::yuy2: {
                // Y0 U Y1 V for each pair of pixels.
                const std::size_t pair = (static_cast<std::size_t>(row) * chroma_w + col / 2) * 4;
                store(i, pixels[pair + (col % 2 ? 2 : 0)], pixels[pair + 1], pixels[pair + 3]);
                break;
            }
            default: break;
            }
        }
    }
    return out;
}

namespace {

// A camera the OS captures (media/video_backend.h).
class EngineCamera final : public CameraSource {
public:
    explicit EngineCamera(std::unique_ptr<CameraEngine> engine)
        : CameraSource(engine->wakes_for_frames()), engine_(std::move(engine)) {}

    // UI thread, right after construction: the engine holds the host weakly.
    void open() { engine_->open(std::shared_ptr<FrameSourceHost>(shared_from_this(), &host_)); }

    void play() override { engine_->start(); }
    void pause() override { engine_->stop(); }
    void seek(double) override {}
    void set_volume(float) override {}
    void set_muted(bool) override {}
    void set_looping(bool) override {}
    bool update() override { return engine_->update(); }
    std::optional<VideoFrame> current() const override { return engine_->current(); }

    std::optional<std::vector<unsigned char>> snapshot() const override {
        const std::optional<VideoFrame> frame = engine_->snapshot_frame();
        if (!frame || !frame->pixels) {
            return std::nullopt;
        }
        const std::vector<std::uint8_t> bgra =
            frame_to_bgra(frame->format, frame->matrix, frame->full_range, *frame->pixels, frame->width, frame->height);
        if (bgra.empty()) {
            return std::nullopt;
        }
        return encode_png(bgra, frame->width, frame->height);
    }

private:
    SourceHost host_{*this}; // outlives the engine
    std::unique_ptr<CameraEngine> engine_;
};

} // namespace

} // namespace elg::detail

namespace elg {

std::vector<CameraDevice> camera_devices() {
    std::vector<CameraDevice> devices;
    for (detail::CameraInfo& camera : detail::system_cameras()) {
        devices.push_back({std::move(camera.id), std::move(camera.name)});
    }
    return devices;
}

Camera::Camera(CameraOptions options) : mirrored_(options.mirrored) {
    auto camera = detail::make_ui_shared<detail::EngineCamera>(
        detail::create_camera_engine({std::move(options.device_id), options.width, options.height}));
    camera->open();
    source_ = std::move(camera);
}

void Camera::start() const {
    source_->play();
}

void Camera::stop() const {
    source_->pause();
}

bool Camera::is_running() const {
    return source_->playing();
}

Size Camera::size() const {
    return source_->size();
}

std::string Camera::error() const {
    return source_->error();
}

std::optional<std::vector<unsigned char>> Camera::snapshot() const {
    return static_cast<const detail::CameraSource&>(*source_).snapshot();
}

} // namespace elg
