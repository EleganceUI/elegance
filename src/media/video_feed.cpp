// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Video feeds (elg/video.h): frames the app pushes, from any thread, to the renderer as
// VideoFrame::Kind::cpu (converted from YUV on the GPU like the camera's).

#include <elg/video.h>

#include "core/ui_tasks.h"
#include "media/video_source.h"

#include <mutex>

namespace elg {

namespace detail {

namespace {

VideoFrame::Format frame_format(VideoFeed::Format format) {
    switch (format) {
    case VideoFeed::Format::bgra: return VideoFrame::Format::bgra;
    case VideoFeed::Format::nv12: return VideoFrame::Format::nv12;
    case VideoFeed::Format::i420: return VideoFrame::Format::i420;
    }
    return VideoFrame::Format::bgra;
}

} // namespace

class FeedSource final : public VideoSource {
public:
    void play() override {}
    void pause() override {}
    void seek(double) override {}
    void set_volume(float) override {}
    void set_muted(bool) override {}
    void set_looping(bool) override {}

    // Any thread.
    void push(VideoFeed::Format format, std::uint32_t width, std::uint32_t height, std::vector<std::uint8_t> pixels,
              bool full_range) {
        const VideoFrame::Format f = frame_format(format);
        if (width == 0 || height == 0 || pixels.size() < packed_size(f, width, height)) {
            return;
        }
        bool resized = false;
        {
            std::lock_guard lock(mutex_);
            resized = width != width_ || height != height_ || !frame_;
            frame_ = std::make_shared<const std::vector<std::uint8_t>>(std::move(pixels));
            format_ = f;
            width_ = width;
            height_ = height;
            full_range_ = full_range;
            ++number_;
        }
        // The render thread checks for frames while it's set.
        playing_now_ = true;
        if (resized) {
            post_ui_task([weak = std::weak_ptr<VideoSource>(shared_from_this()), width, height] {
                if (const auto self = weak.lock()) {
                    self->size = Size{static_cast<float>(width), static_cast<float>(height)};
                    self->ready = true;
                    self->playing = true;
                }
            });
        }
    }

    void clear() {
        {
            std::lock_guard lock(mutex_);
            if (!frame_) {
                return;
            }
            frame_.reset();
            width_ = height_ = 0;
            ++number_;
        }
        playing_now_ = false;
        post_ui_task([weak = std::weak_ptr<VideoSource>(shared_from_this())] {
            if (const auto self = weak.lock()) {
                self->size = Size{};
                self->ready = false;
                self->playing = false;
            }
        });
    }

    // Render thread.
    bool update() override {
        std::lock_guard lock(mutex_);
        if (number_ == reported_) {
            return false;
        }
        reported_ = number_;
        return true;
    }

    std::optional<VideoFrame> current() const override {
        std::lock_guard lock(mutex_);
        if (!frame_) {
            return std::nullopt;
        }
        VideoFrame frame;
        frame.kind = VideoFrame::Kind::cpu;
        frame.width = width_;
        frame.height = height_;
        frame.format = format_;
        frame.matrix = height_ >= 720 ? VideoFrame::Matrix::bt709 : VideoFrame::Matrix::bt601;
        frame.full_range = full_range_;
        frame.pixels = frame_;
        frame.source_id = id;
        frame.frame_id = number_;
        return frame;
    }

private:
    mutable std::mutex mutex_;
    std::shared_ptr<const std::vector<std::uint8_t>> frame_;
    VideoFrame::Format format_ = VideoFrame::Format::bgra;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    bool full_range_ = false;
    std::uint64_t number_ = 0;
    std::uint64_t reported_ = 0;
};

} // namespace detail

VideoFeed::VideoFeed() : source_(std::make_shared<detail::FeedSource>()) {}

void VideoFeed::push(Format format, std::uint32_t width, std::uint32_t height, std::vector<std::uint8_t> pixels,
                     bool full_range) const {
    static_cast<detail::FeedSource&>(*source_).push(format, width, height, std::move(pixels), full_range);
}

void VideoFeed::clear() const {
    static_cast<detail::FeedSource&>(*source_).clear();
}

Size VideoFeed::size() const {
    return source_->size();
}

bool VideoFeed::has_frame() const {
    return source_->ready();
}

} // namespace elg
