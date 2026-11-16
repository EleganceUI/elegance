// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Video feeds (elg/video.h): frames the app pushes, from any thread, to the renderer as
// VideoFrame::Kind::cpu (converted from YUV on the GPU like the camera's).

#include <elg/video.h>

#include "core/ui_tasks.h"
#include "media/video_source.h"

#include <mutex>
#include <utility>

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

// Pushing and clearing wake the render thread: it never looks for frames on its own.
class FeedSource final : public VideoSource {
public:
    FeedSource() : VideoSource(true) {}

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
        bool post = false;
        {
            const std::lock_guard lock(mutex_);
            const bool resized = width != width_ || height != height_ || !frame_;
            frame_ = std::make_shared<const std::vector<std::uint8_t>>(std::move(pixels));
            format_ = f;
            width_ = width;
            height_ = height;
            full_range_ = full_range;
            ++number_;
            playing_now_ = true;
            post = resized && !std::exchange(sync_pending_, true);
        }
        notify_new_frame();
        if (post) {
            post_sync();
        }
    }

    void clear() {
        bool post = false;
        {
            const std::lock_guard lock(mutex_);
            if (!frame_) {
                return;
            }
            frame_.reset();
            width_ = height_ = 0;
            ++number_;
            playing_now_ = false;
            post = !std::exchange(sync_pending_, true);
        }
        notify_new_frame();
        if (post) {
            post_sync();
        }
    }

    // Render thread.
    bool update() override {
        const std::lock_guard lock(mutex_);
        if (number_ == reported_) {
            return false;
        }
        reported_ = number_;
        return true;
    }

    std::optional<VideoFrame> current() const override {
        const std::lock_guard lock(mutex_);
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
    // The calls that ran last under the lock decide what windows show, whatever the threads:
    // - The render thread: each call changes the frame under the lock, then wakes the render
    //   thread, which then takes the frame under the lock. A wake that comes while the render
    //   thread is busy isn't lost (FrameMailbox keeps it), so after the last call's wake the
    //   render thread draws what the last call left, or something newer.
    // - The views' size and flags (UI thread): a call that changes the size or whether there's
    //   a frame posts a sync() unless one is pending. sync() clears `sync_pending_` and reads the
    //   state under the lock, so the pending one reads every change made before it runs. UI
    //   tasks run in order, so the last one to run reads the final state.
    void post_sync() {
        post_ui_task([weak = std::weak_ptr<FeedSource>(std::static_pointer_cast<FeedSource>(shared_from_this()))] {
            if (const auto self = weak.lock()) {
                self->sync();
            }
        });
    }
    // UI thread: the views' size and flags, as the feed is now.
    void sync() {
        Size frame_size;
        bool has_frame = false;
        {
            const std::lock_guard lock(mutex_);
            sync_pending_ = false;
            has_frame = frame_ != nullptr;
            frame_size = Size{static_cast<float>(width_), static_cast<float>(height_)};
        }
        size = frame_size;
        ready = has_frame;
        playing = has_frame;
    }

    mutable std::mutex mutex_;
    bool sync_pending_ = false; // a sync() is posted and hasn't read the state yet
    std::shared_ptr<const std::vector<std::uint8_t>> frame_;
    VideoFrame::Format format_ = VideoFrame::Format::bgra;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    bool full_range_ = false;
    std::uint64_t number_ = 0;
    std::uint64_t reported_ = 0;
};

} // namespace detail

VideoFeed::VideoFeed() : source_(detail::make_ui_shared<detail::FeedSource>()) {}

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
