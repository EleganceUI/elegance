// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include <elg/gpu_image.h>

#include "render/gpu_image.h"

#include "core/ui_tasks.h"
#include "gpu/external_image.h"
#include "gpu/fence_backend.h"
#include "gpu/native_image.h"
#include "media/video_source.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>

namespace elg::detail {
namespace {
std::atomic<std::uint64_t> next_external_id{1ull << 61};
} // namespace

bool imports_native_frames() {
    return gpu::imports_native_images();
}

std::optional<VideoFrame> import_native_frame(const VideoFrame& native) {
    if (native.kind != VideoFrame::Kind::native || !native.native) {
        return std::nullopt;
    }
    gpu::NativeImport imported = gpu::import_native_image(native.native.get());
    if (!imported.image) {
        return std::nullopt;
    }
    // As a GpuImage's published frame.
    VideoFrame frame;
    frame.kind = VideoFrame::Kind::external;
    frame.width = imported.width;
    frame.height = imported.height;
    frame.source_id = native.source_id;
    frame.texture_id = next_external_id.fetch_add(1, std::memory_order_relaxed);
    frame.frame_id = frame.texture_id;
    imported.image->accept(); // its release follows the last reference
    frame.external = std::move(imported.image);
    return frame;
}

bool imported_frame_current(const VideoFrame& frame) {
    return frame.external && gpu::in_current_device(*frame.external);
}

bool valid_vulkan_fence_value(std::uint64_t value, bool timeline) noexcept {
    return timeline ? value >= 1 : value == 1;
}


// A GPU image's frames: the last published one, until it's cleared. Publishing and clearing wake
// the render thread: it never looks for frames on its own.
class ExternalImageFrames final : public FrameSource {
public:
    explicit ExternalImageFrames(Size natural_size) : FrameSource(true), natural_size_(natural_size) {}

    // Any thread. When the size changes and no task for the views' size is pending,
    // `post_size_task` posts one, which reads the size with take_size().
    template <typename PostSizeTask>
    std::uint32_t publish(const ElgGpuImageFrame& input, PostSizeTask post_size_task) {
        if (!input.width || !input.height || !input.release ||
            (input.format != ELG_GPU_IMAGE_RGBA8 && input.format != ELG_GPU_IMAGE_BGRA8) ||
            input.transport < ELG_GPU_IMAGE_IOSURFACE || input.transport > ELG_GPU_IMAGE_VULKAN_OPAQUE_FD ||
            input.vk_dedicated_allocation > 1) {
            return ELG_GPU_IMAGE_INVALID_ARGUMENT;
        }
        if (input.transport == ELG_GPU_IMAGE_VULKAN_OPAQUE_FD) {
                    if (input.memory_fd < 0 || !input.vk_image_create_info || !input.vk_allocation_size) {
                        return ELG_GPU_IMAGE_INVALID_ARGUMENT;
                    }
                } else if (!input.handle) {
            return ELG_GPU_IMAGE_INVALID_ARGUMENT;
        }
        const auto& fence = input.producer_fence;
        // Transport tags intentionally match their corresponding fence tags.
        if (fence.type != ELG_GPU_IMAGE_FENCE_NONE &&
            (fence.type != input.transport ||
             (fence.type == ELG_GPU_IMAGE_FENCE_VK_OPAQUE_FD
                  ? fence.fd < 0 || !valid_vulkan_fence_value(fence.value, gpu::vulkan_fences_are_timelines())
                  : !fence.handle))) {
            return ELG_GPU_IMAGE_INVALID_ARGUMENT;
        }
        std::shared_ptr<gpu::ExternalImage> external;
        if (const std::uint32_t status = gpu::import_external_image(input, external); status != ELG_GPU_IMAGE_OK) {
            return status;
        }
        VideoFrame frame;
        frame.kind = VideoFrame::Kind::external;
        frame.width = input.width;
        frame.height = input.height;
        frame.source_id = id;
        frame.texture_id = next_external_id.fetch_add(1, std::memory_order_relaxed);
        frame.frame_id = frame.texture_id;
        frame.external = external;
        std::optional<VideoFrame> old;
        {
            const std::lock_guard lock(mutex_);
            const Size next_size{static_cast<float>(input.width), static_cast<float>(input.height)};
            if (!size_task_pending_ && (natural_size_.width != next_size.width ||
                                       natural_size_.height != next_size.height)) {
                // Queue before accepting ownership so allocation failure leaves the old frame
                // intact. The task takes this mutex only after publication commits.
                post_size_task();
                size_task_pending_ = true;
            }
            external->accept();
            old.swap(frame_);
            frame_ = std::move(frame);
            natural_size_ = next_size;
            ++number_;
        }
        notify_new_frame();
        return ELG_GPU_IMAGE_OK; // old callbacks run outside the source lock
    }
    void clear() {
        std::optional<VideoFrame> old;
        {
            const std::lock_guard lock(mutex_);
            if (!frame_) {
                return; // nothing shown: nothing changes
            }
            old.swap(frame_);
            ++number_;
        }
        notify_new_frame();
    }
    // UI thread, in the task publish() posted: the latest size. A size that changes after this
    // posts the next task.
    Size take_size() {
        const std::lock_guard lock(mutex_);
        size_task_pending_ = false;
        return natural_size_;
    }
    bool update() override {
        const std::lock_guard lock(mutex_);
        return std::exchange(reported_, number_) != number_;
    }
    std::optional<VideoFrame> current() const override {
        const std::lock_guard lock(mutex_);
        return frame_; // strong snapshot reservation, not a weak owner notification
    }
private:
    mutable std::mutex mutex_;
    Size natural_size_;
    bool size_task_pending_ = false;
    std::optional<VideoFrame> frame_;
    std::uint64_t number_ = 0, reported_ = 0;
};

// A GPU image's views' states, and its frames.
class ExternalImageSource final : public VideoSource {
public:
    ExternalImageSource(std::uint32_t width, std::uint32_t height, bool flip)
        : flipped(flip),
          frames_(std::make_shared<ExternalImageFrames>(Size{static_cast<float>(width), static_cast<float>(height)})) {
        size = Size{static_cast<float>(width), static_cast<float>(height)};
        ready = true;
    }
    std::shared_ptr<FrameSource> frames() override { return frames_; }
    void play() override {}
    void pause() override {}
    void seek(double) override {}
    void set_volume(float) override {}
    void set_muted(bool) override {}
    void set_looping(bool) override {}

    // Any thread.
    std::uint32_t publish(const ElgGpuImageFrame& input) {
        return frames_->publish(input, [this] {
            post_ui_task([weak = std::weak_ptr<ExternalImageSource>(
                              std::static_pointer_cast<ExternalImageSource>(shared_from_this()))] {
                if (const auto source = weak.lock()) {
                    // Read the latest metadata, not captured dimensions or a GPU frame.
                    // A racing publication queues the next task; UI tasks run in order.
                    const Size latest = source->frames_->take_size();
                    const auto previous = source->size.peek();
                    if (previous.width != latest.width || previous.height != latest.height) {
                        source->size = latest;
                    }
                }
            });
        });
    }
    void clear() { frames_->clear(); }

    const bool flipped;

private:
    const std::shared_ptr<ExternalImageFrames> frames_;
};
} // namespace elg::detail

struct ElgGpuImage {
    std::shared_ptr<elg::detail::ExternalImageSource> source;
};

extern "C" {
ElgGpuImage* elg_gpu_image_create(uint32_t width, uint32_t height, uint32_t origin) {
    if (!width || !height || origin > ELG_GPU_IMAGE_BOTTOM_LEFT) {
        return nullptr;
    }
    try {
        return new ElgGpuImage{elg::detail::make_ui_shared<elg::detail::ExternalImageSource>(
            width, height, origin == ELG_GPU_IMAGE_BOTTOM_LEFT)};
    } catch (...) { return nullptr; }
}
void elg_gpu_image_destroy(ElgGpuImage* image) { delete image; }
uint32_t elg_gpu_image_publish(ElgGpuImage* image, const ElgGpuImageFrame* frame) {
    if (!image || !frame) {
        return ELG_GPU_IMAGE_INVALID_ARGUMENT;
    }
    try { return image->source->publish(*frame); }
    catch (...) { return ELG_GPU_IMAGE_INTERNAL_ERROR; }
}
void elg_gpu_image_clear(ElgGpuImage* image) {
    if (image) {
        image->source->clear();
    }
}
}

namespace elg {
std::shared_ptr<detail::VideoSource> detail::gpu_image_source(const ElgGpuImage* image) {
    return image ? image->source : nullptr;
}
bool gpu_image_flipped(const ElgGpuImage* image) {
    return image && image->source->flipped;
}
}
