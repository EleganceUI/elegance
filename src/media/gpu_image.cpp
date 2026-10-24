// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include <elg/gpu_image.h>

#include "gpu/device.h"
#include "media/video_source.h"

#include <array>
#include <atomic>
#include <mutex>

namespace elg {

namespace detail {

namespace {

std::atomic<std::uint64_t> next_texture_id{1ull << 62}; // apart from decoders' ids

} // namespace

class GpuImageSource final : public VideoSource {
public:
    explicit GpuImageSource(GpuImage::Options options) : options_(options) {
        size = Size{static_cast<float>(options.width), static_cast<float>(options.height)};
        ready = true;
    }

    const GpuImage::Options& options() const noexcept { return options_; }

    void play() override {}
    void pause() override {}
    void seek(double) override {}
    void set_volume(float) override {}
    void set_muted(bool) override {}
    void set_looping(bool) override {}

    // --- The app's drawing thread ---

    std::optional<GpuImage::Frame> begin_frame() {
        if (options_.width == 0 || options_.height == 0) {
            return std::nullopt;
        }
        // Every access to renderer_ and the reservation state uses mutex_. Lock order is
        // source -> renderer; shutdown never takes a source mutex. Concurrent begin calls
        // therefore initialize/pin exactly one renderer and grant at most one reservation.
        std::lock_guard lock(mutex_);
        if (!renderer_) {
            renderer_ = gpu::render_device();
        }
        if (!renderer_) {
            return std::nullopt;
        }
        // Resource creation and the active check cannot race renderer shutdown. The app must
        // still stop its producer before shutdown: returned handles are borrowed, not a lock.
        std::lock_guard device_lock(renderer_->mutex);
        if (!renderer_->active) {
            return std::nullopt;
        }
        if (writing_ >= 0 || !create()) {
            return std::nullopt;
        }
        int chosen = -1;
        for (int i = 0; i < static_cast<int>(GpuImage::kBuffers); ++i) {
            const Buffer& buffer = buffers_[i];
            if (i == current_ || i == latest_ || buffer.readers != 0) {
                continue;
            }
            if (chosen < 0 || buffer.written < buffers_[chosen].written) {
                chosen = i;
            }
        }
        if (chosen < 0) {
            return std::nullopt;
        }
        writing_ = chosen;
        GpuImage::Frame frame;
        frame.buffer = static_cast<std::uint32_t>(chosen);
        frame.width = options_.width;
        frame.height = options_.height;
        frame.device = renderer_->device.Get();
        frame.queue = renderer_->queue.Get();
        frame.texture = buffers_[chosen].texture.Get();
        frame.sequence = ++sequence_;
        return frame;
    }

    void end_frame(const GpuImage::Frame& frame, bool publish) {
        std::lock_guard lock(mutex_);
        if (writing_ < 0 || frame.buffer != static_cast<std::uint32_t>(writing_) ||
            frame.sequence != sequence_ || frame.texture != buffers_[writing_].texture.Get() ||
            frame.device != renderer_->device.Get() || frame.queue != renderer_->queue.Get() ||
            frame.width != options_.width || frame.height != options_.height) {
            return;
        }
        if (publish) {
            // The producer submitted first. This mutex publishes that fact to current(), which
            // reserves the buffer until VideoTextures::release() after renderer submission.
            buffers_[writing_].written = ++number_;
            latest_ = writing_;
            playing_now_ = true;
        }
        writing_ = -1;
    }

    // --- Render thread ---

    bool update() override {
        std::lock_guard lock(mutex_);
        if (latest_ >= 0) {
            current_ = latest_;
            latest_ = -1;
        }
        if (number_ == reported_) {
            return false;
        }
        reported_ = number_;
        return true;
    }

    std::optional<VideoFrame> current() const override {
        std::lock_guard lock(mutex_);
        if (current_ < 0) {
            return std::nullopt;
        }
        Buffer& buffer = buffers_[current_];
        VideoFrame frame;
        frame.kind = VideoFrame::Kind::webgpu;
        frame.format = VideoFrame::Format::rgba;
        frame.width = options_.width;
        frame.height = options_.height;
        frame.source_id = id;
        frame.texture_id = buffer.texture_id;
        frame.frame_id = buffer.written;
        frame.device = renderer_->device;
        frame.texture = buffer.texture;
        auto self = std::static_pointer_cast<GpuImageSource>(
            std::const_pointer_cast<VideoSource>(shared_from_this()));
        // Reserve NOW, not in acquire(): another window can advance current_ while a snapshot
        // still exists. Retain the source as well as the texture until the last lease is gone.
        frame.texture_lease = std::make_shared<ReadLease>(std::move(self), current_);
        ++buffer.readers;
        return frame;
    }

private:
    struct ReadLease {
        ReadLease(std::shared_ptr<GpuImageSource> source, int index)
            : source(std::move(source)), index(index) {}
        ~ReadLease() {
            std::lock_guard lock(source->mutex_);
            --source->buffers_[index].readers;
        }
        std::shared_ptr<GpuImageSource> source;
        int index;
    };

    struct Buffer {
        wgpu::Texture texture;
        std::uint64_t texture_id = 0;
        std::uint64_t written = 0;
        std::uint32_t readers = 0; // snapshots/recordings not yet submitted
    };

    // Called with mutex_ then renderer_->mutex held; concurrent begin calls are serialized.
    bool create() {
        if (buffers_[0].texture) {
            return true;
        }
        wgpu::Limits limits;
        renderer_->device.GetLimits(&limits);
        if (options_.width > limits.maxTextureDimension2D || options_.height > limits.maxTextureDimension2D) {
            return false;
        }
        constexpr wgpu::TextureFormat sampled = wgpu::TextureFormat::RGBA8UnormSrgb;
        wgpu::TextureDescriptor descriptor;
        descriptor.label = "GpuImage";
        descriptor.dimension = wgpu::TextureDimension::e2D;
        descriptor.size = {options_.width, options_.height, 1};
        descriptor.format = wgpu::TextureFormat::RGBA8Unorm;
        descriptor.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::RenderAttachment |
                           wgpu::TextureUsage::CopySrc | wgpu::TextureUsage::CopyDst |
                           wgpu::TextureUsage::StorageBinding;
        descriptor.viewFormatCount = 1;
        descriptor.viewFormats = &sampled;
        // Commit only the complete ring, including when creation throws or returns null.
        std::array<Buffer, GpuImage::kBuffers> buffers;
        for (Buffer& buffer : buffers) {
            buffer.texture = renderer_->device.CreateTexture(&descriptor);
            if (!buffer.texture) {
                return false;
            }
            buffer.texture_id = next_texture_id++;
        }
        buffers_ = std::move(buffers);
        return true;
    }

    const GpuImage::Options options_;
    mutable std::mutex mutex_;
    std::shared_ptr<gpu::RenderDevice> renderer_; // guarded by mutex_; pinned, never migrates
    mutable std::array<Buffer, GpuImage::kBuffers> buffers_;
    std::uint64_t sequence_ = 0;
    int writing_ = -1;
    int latest_ = -1;
    int current_ = -1;
    std::uint64_t number_ = 0;
    std::uint64_t reported_ = 0;
};

} // namespace detail

GpuImage::GpuImage(Options options) : source_(std::make_shared<detail::GpuImageSource>(options)) {}

std::optional<GpuImage::Frame> GpuImage::begin_frame() const {
    return source_->begin_frame();
}

void GpuImage::end_frame(const Frame& frame) const {
    source_->end_frame(frame, true);
}

void GpuImage::cancel_frame(const Frame& frame) const {
    source_->end_frame(frame, false);
}

Size GpuImage::size() const noexcept {
    return {static_cast<float>(source_->options().width), static_cast<float>(source_->options().height)};
}

GpuImage::Origin GpuImage::origin() const noexcept {
    return source_->options().origin;
}

std::shared_ptr<detail::VideoSource> GpuImage::source() const noexcept {
    return source_;
}

} // namespace elg
