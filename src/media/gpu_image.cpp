// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
#include <elg/gpu_image.h>

#include "core/ui_tasks.h"
#include "gpu/device.h"
#include "media/video_source.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace elg::detail {
namespace {
std::atomic<std::uint64_t> next_external_id{1ull << 61};
constexpr std::uint64_t kRetirementWaitNs = 5'000'000'000ull;

// EndAccess can return a Metal scheduling future. Waiting on it from a render-side lease
// destructor can stall the very thread that must keep submitting/ticking. One worker retains
// imported objects and callback state until retirement finishes, without blocking render/UI.
class RetirementWorker {
public:
    RetirementWorker() : thread_([this] { run(); }) {}
    ~RetirementWorker() {
        {
            std::lock_guard lock(mutex_);
            stopping_ = true;
        }
        wake_.notify_one();
        thread_.join();
    }
    void enqueue(std::function<void()> job) {
        {
            std::lock_guard lock(mutex_);
            jobs_.push_back(std::move(job));
        }
        wake_.notify_one();
    }
private:
    void run() {
        for (;;) {
            std::function<void()> job;
            {
                std::unique_lock lock(mutex_);
                wake_.wait(lock, [this] { return stopping_ || !jobs_.empty(); });
                if (jobs_.empty()) return;
                job = std::move(jobs_.front());
                jobs_.pop_front();
            }
            job(); // retirement catches failures; foreign callbacks never run under our locks
        }
    }
    std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<std::function<void()>> jobs_;
    bool stopping_ = false;
    std::thread thread_;
};
RetirementWorker& retirement_worker() {
    static RetirementWorker worker;
    return worker;
}

// Native import validation is a caller error, not an uncaptured renderer error. Keep the
// instance alive with the device so retirement can also capture errors after views disappear.
class ValidationScope {
public:
    explicit ValidationScope(const gpu::RenderDevice& renderer) : renderer_(renderer) {
        renderer_.device.PushErrorScope(wgpu::ErrorFilter::Validation);
    }
    ~ValidationScope() {
        if (!popped_) {
            renderer_.device.PopErrorScope(wgpu::CallbackMode::AllowSpontaneous,
                [](wgpu::PopErrorScopeStatus, wgpu::ErrorType, wgpu::StringView) {});
        }
    }
    bool finish() {
        auto valid = std::make_shared<bool>(false);
        const auto future = renderer_.device.PopErrorScope(wgpu::CallbackMode::WaitAnyOnly,
            [valid](wgpu::PopErrorScopeStatus status, wgpu::ErrorType error, wgpu::StringView) {
                *valid = status == wgpu::PopErrorScopeStatus::Success && error == wgpu::ErrorType::NoError;
            });
        popped_ = true;
        return renderer_.instance.WaitAny(future, kRetirementWaitNs) ==
                   wgpu::WaitStatus::Success && *valid;
    }
private:
    const gpu::RenderDevice& renderer_;
    bool popped_ = false;
};

wgpu::FeatureName memory_feature(std::uint32_t transport) {
    switch (transport) {
    case ELG_GPU_IMAGE_IOSURFACE: return wgpu::FeatureName::SharedTextureMemoryIOSurface;
    case ELG_GPU_IMAGE_DXGI_HANDLE: return wgpu::FeatureName::SharedTextureMemoryDXGISharedHandle;
    default: return wgpu::FeatureName::SharedTextureMemoryOpaqueFD;
    }
}
wgpu::FeatureName fence_feature(std::uint32_t transport) {
    switch (transport) {
    case ELG_GPU_IMAGE_IOSURFACE: return wgpu::FeatureName::SharedFenceMTLSharedEvent;
    case ELG_GPU_IMAGE_DXGI_HANDLE: return wgpu::FeatureName::SharedFenceDXGISharedHandle;
    default: return wgpu::FeatureName::SharedFenceVkSemaphoreOpaqueFD;
    }
}

class ExternalFrame {
public:
    ExternalFrame(ElgGpuImageFrame descriptor, std::shared_ptr<gpu::RenderDevice> renderer)
        : descriptor_(descriptor), renderer_(std::move(renderer)) {
        // This pointer and its chain are only valid for the synchronous import call.
        descriptor_.vk_image_create_info = nullptr;
    }
    ~ExternalFrame() {
        if (!begun_) return;
        try {
            retirement_worker().enqueue([descriptor = descriptor_, renderer = renderer_,
                                         memory = memory_, image = texture, view = sample_view,
                                         accepted = accepted_] {
                retire(descriptor, renderer, memory, image, accepted);
            });
        } catch (...) {
            ElgGpuImageRelease result{};
            result.token = descriptor_.token;
            result.status = ELG_GPU_IMAGE_INTERNAL_ERROR;
            notify(descriptor_, accepted_, result);
        }
    }

    static void notify(const ElgGpuImageFrame& descriptor, bool accepted,
                       const ElgGpuImageRelease& result) noexcept {
        if (accepted) {
            // A foreign callback is forbidden to unwind. Even a violating C++ callback must
            // not cause a second ownership-transfer callback from our exception handler.
            try { descriptor.release(descriptor.userdata, &result); } catch (...) {}
        }
    }
    static void retire(const ElgGpuImageFrame& descriptor_,
                       const std::shared_ptr<gpu::RenderDevice>& renderer_,
                       const wgpu::SharedTextureMemory& memory_, const wgpu::Texture& texture,
                       bool accepted_) noexcept {
        ElgGpuImageRelease result{};
        result.token = descriptor_.token;
        result.status = ELG_GPU_IMAGE_SYNC_FAILED;
        try {
            std::vector<ElgGpuImageFence> fences;
            // Keep the EndAccess state (and its owning SharedFence references) alive throughout
            // callback execution. ExportInfo's native handles are borrowed from these objects.
            wgpu::SharedTextureMemoryEndAccessState end;
            wgpu::SharedTextureMemoryVkImageLayoutEndState vk;
            wgpu::SharedTextureMemoryMetalEndAccessState metal;
            std::unique_ptr<ValidationScope> scope;
            bool ok = false;
            {
                std::lock_guard lock(renderer_->mutex);
                // active gates new publications only. This frame pins the Dawn device
                // and instance through EndAccess/export, even after UI shutdown.
                {
                    scope = std::make_unique<ValidationScope>(*renderer_);
                    if (descriptor_.transport == ELG_GPU_IMAGE_VULKAN_OPAQUE_FD) {
                        end.nextInChain = &vk;
                    } else if (descriptor_.transport == ELG_GPU_IMAGE_IOSURFACE) {
                        end.nextInChain = &metal;
                    }
                    ok = memory_.EndAccess(texture, &end) == wgpu::Status::Success;
                    result.vk_old_layout = vk.oldLayout;
                    result.vk_new_layout = vk.newLayout;
                    ok = ok && end.fenceCount == end.signaledValueCount;
                    for (std::size_t i = 0; ok && i < end.fenceCount; ++i) {
                        ElgGpuImageFence fence{};
                        fence.value = end.signaledValues[i];
                        wgpu::SharedFenceExportInfo info;
                        end.fences[i].ExportInfo(&info);
                        wgpu::SharedFenceMTLSharedEventExportInfo mtl;
                        wgpu::SharedFenceDXGISharedHandleExportInfo dxgi;
                        wgpu::SharedFenceVkSemaphoreOpaqueFDExportInfo fd;
                        if (info.type == wgpu::SharedFenceType::MTLSharedEvent) {
                            info.nextInChain = &mtl;
                            end.fences[i].ExportInfo(&info);
                            fence.type = ELG_GPU_IMAGE_FENCE_MTL_SHARED_EVENT;
                            fence.handle = mtl.sharedEvent;
                            ok = fence.handle != nullptr;
                        } else if (info.type == wgpu::SharedFenceType::DXGISharedHandle) {
                            info.nextInChain = &dxgi;
                            end.fences[i].ExportInfo(&info);
                            fence.type = ELG_GPU_IMAGE_FENCE_DXGI_HANDLE;
                            fence.handle = dxgi.handle;
                            ok = fence.handle != nullptr;
                        } else if (info.type == wgpu::SharedFenceType::VkSemaphoreOpaqueFD) {
                            info.nextInChain = &fd;
                            end.fences[i].ExportInfo(&info);
                            fence.type = ELG_GPU_IMAGE_FENCE_VK_OPAQUE_FD;
                            fence.fd = fd.handle;
                            ok = fence.fd >= 0;
                        } else {
                            ok = false; // never silently drop an unexportable dependency
                        }
                        fences.push_back(fence);
                    }
                }
            }
            if (scope) {
                // No renderer mutex is held across ANY wait, including error-scope completion.
                const bool valid = scope->finish();
                ok = ok && valid;
                if (ok && metal.commandsScheduledFuture.id != 0) {
                    ok = renderer_->instance.WaitAny(metal.commandsScheduledFuture,
                        kRetirementWaitNs) == wgpu::WaitStatus::Success;
                }
                result.status = ok ? ELG_GPU_IMAGE_OK : ELG_GPU_IMAGE_SYNC_FAILED;

            }
            if (result.status == ELG_GPU_IMAGE_OK) {
                result.fences = fences.data();
                result.fence_count = fences.size();
            }
            // No internal locks held during foreign code. The contract still forbids reentry.
            notify(descriptor_, accepted_, result);
        } catch (...) {
            result.status = ELG_GPU_IMAGE_INTERNAL_ERROR;
            result.fences = nullptr;
            result.fence_count = 0;
            notify(descriptor_, accepted_, result);
        }
    }

    std::uint32_t import(const ElgGpuImageFrame& frame) {
        const auto& dev = renderer_->device;
        if (!dev.HasFeature(memory_feature(frame.transport)) || !dev.HasFeature(fence_feature(frame.transport))) {
            return ELG_GPU_IMAGE_UNSUPPORTED;
        }
        // D3D11 often cannot reinterpret typed UNORM as sRGB without a copy. This API never
        // silently copies; restrict DXGI to D3D12 rather than promise D3D11 support.
        const auto backend = renderer_->backend;
        if ((frame.transport == ELG_GPU_IMAGE_IOSURFACE && backend != wgpu::BackendType::Metal) ||
            (frame.transport == ELG_GPU_IMAGE_DXGI_HANDLE && backend != wgpu::BackendType::D3D12) ||
            (frame.transport == ELG_GPU_IMAGE_VULKAN_OPAQUE_FD && backend != wgpu::BackendType::Vulkan)) {
            return ELG_GPU_IMAGE_UNSUPPORTED;
        }
        ValidationScope scope(*renderer_);
        wgpu::SharedTextureMemoryIOSurfaceDescriptor io;
        io.ioSurface = frame.handle;
        io.allowStorageBinding = false;
        wgpu::SharedTextureMemoryDXGISharedHandleDescriptor dxgi;
        dxgi.handle = frame.handle;
        wgpu::SharedTextureMemoryOpaqueFDDescriptor fd;
        fd.vkImageCreateInfo = frame.vk_image_create_info;
        fd.memoryFD = frame.memory_fd;
        fd.memoryTypeIndex = frame.vk_memory_type_index;
        fd.allocationSize = frame.vk_allocation_size;
        fd.dedicatedAllocation = frame.vk_dedicated_allocation != 0;
        wgpu::SharedTextureMemoryDescriptor memory;
        switch (frame.transport) {
        case ELG_GPU_IMAGE_IOSURFACE: memory.nextInChain = &io; break;
        case ELG_GPU_IMAGE_DXGI_HANDLE: memory.nextInChain = &dxgi; break;
        default: memory.nextInChain = &fd; break;
        }
        memory_ = dev.ImportSharedTextureMemory(&memory);
        wgpu::SharedTextureMemoryProperties properties;
        bool ok = memory_ && memory_.GetProperties(&properties) == wgpu::Status::Success;
        const auto stored = frame.format == ELG_GPU_IMAGE_RGBA8 ? wgpu::TextureFormat::RGBA8Unorm
                                                              : wgpu::TextureFormat::BGRA8Unorm;
        const auto sampled = frame.format == ELG_GPU_IMAGE_RGBA8 ? wgpu::TextureFormat::RGBA8UnormSrgb
                                                               : wgpu::TextureFormat::BGRA8UnormSrgb;
        ok = ok && properties.format == stored && properties.size.width == frame.width &&
             properties.size.height == frame.height && properties.size.depthOrArrayLayers == 1 &&
             (properties.usage & wgpu::TextureUsage::TextureBinding) != wgpu::TextureUsage::None;
        if (ok) {
            wgpu::TextureDescriptor desc;
            desc.size = {frame.width, frame.height, 1};
            desc.format = stored;
            desc.usage = wgpu::TextureUsage::TextureBinding;
            desc.viewFormatCount = 1;
            desc.viewFormats = &sampled;
            texture = memory_.CreateTexture(&desc);
            wgpu::TextureViewDescriptor view;
            view.format = sampled;
            view.usage = wgpu::TextureUsage::TextureBinding;
            sample_view = texture.CreateView(&view);
            ok = texture && sample_view;
        }
        wgpu::SharedFence wait;
        if (ok && frame.producer_fence.type != ELG_GPU_IMAGE_FENCE_NONE) {
            wgpu::SharedFenceMTLSharedEventDescriptor mtl;
            mtl.sharedEvent = frame.producer_fence.handle;
            wgpu::SharedFenceDXGISharedHandleDescriptor shared;
            shared.handle = frame.producer_fence.handle;
            wgpu::SharedFenceVkSemaphoreOpaqueFDDescriptor semaphore;
            semaphore.handle = frame.producer_fence.fd;
            wgpu::SharedFenceDescriptor desc;
            switch (frame.producer_fence.type) {
            case ELG_GPU_IMAGE_FENCE_MTL_SHARED_EVENT: desc.nextInChain = &mtl; break;
            case ELG_GPU_IMAGE_FENCE_DXGI_HANDLE: desc.nextInChain = &shared; break;
            default: desc.nextInChain = &semaphore; break;
            }
            wait = dev.ImportSharedFence(&desc);
            ok = bool(wait);
        }
        if (ok) {
            wgpu::SharedTextureMemoryBeginAccessDescriptor access;
            access.initialized = true;
            access.concurrentRead = false;
            if (wait) {
                access.fenceCount = access.signaledValueCount = 1;
                access.fences = &wait;
                access.signaledValues = &frame.producer_fence.value;
            }
            wgpu::SharedTextureMemoryVkImageLayoutBeginState vk;
            vk.oldLayout = frame.vk_old_layout;
            vk.newLayout = frame.vk_new_layout;
            if (frame.transport == ELG_GPU_IMAGE_VULKAN_OPAQUE_FD) access.nextInChain = &vk;
            begun_ = memory_.BeginAccess(texture, &access) == wgpu::Status::Success;
            ok = begun_;
        }
        const bool valid = scope.finish();
        return ok && valid ? ELG_GPU_IMAGE_OK : ELG_GPU_IMAGE_IMPORT_FAILED;
    }
    void accept() noexcept { accepted_ = true; }
    wgpu::Texture texture;
    wgpu::TextureView sample_view;
private:
    ElgGpuImageFrame descriptor_;
    std::shared_ptr<gpu::RenderDevice> renderer_;
    wgpu::SharedTextureMemory memory_;
    bool begun_ = false;
    bool accepted_ = false;
};
} // namespace

class ExternalImageSource final : public VideoSource {
public:
    ExternalImageSource(std::uint32_t width, std::uint32_t height, bool flip)
        : flipped(flip), natural_size_{static_cast<float>(width), static_cast<float>(height)} {
        size = natural_size_;
        ready = true;
        // Keep the render-side update polling even after clear, so a cleared last image is
        // actually removed without requiring a UI state mutation from the producer thread.
        playing_now_ = true;
    }
    void play() override {}
    void pause() override {}
    void seek(double) override {}
    void set_volume(float) override {}
    void set_muted(bool) override {}
    void set_looping(bool) override {}

    std::uint32_t publish(const ElgGpuImageFrame& input) {
        if (!input.width || !input.height || !input.release ||
            (input.format != ELG_GPU_IMAGE_RGBA8 && input.format != ELG_GPU_IMAGE_BGRA8) ||
            input.transport < ELG_GPU_IMAGE_IOSURFACE || input.transport > ELG_GPU_IMAGE_VULKAN_OPAQUE_FD ||
            input.vk_dedicated_allocation > 1) return ELG_GPU_IMAGE_INVALID_ARGUMENT;
        if (input.transport == ELG_GPU_IMAGE_VULKAN_OPAQUE_FD) {
            if (input.memory_fd < 0 || !input.vk_image_create_info || !input.vk_allocation_size)
                return ELG_GPU_IMAGE_INVALID_ARGUMENT;
        } else if (!input.handle) return ELG_GPU_IMAGE_INVALID_ARGUMENT;
        const auto& fence = input.producer_fence;
        // Transport tags intentionally match their corresponding fence tags.
        if (fence.type != ELG_GPU_IMAGE_FENCE_NONE &&
            (fence.type != input.transport ||
             (fence.type == ELG_GPU_IMAGE_FENCE_VK_OPAQUE_FD ? fence.fd < 0 || fence.value != 1 : !fence.handle)))
            return ELG_GPU_IMAGE_INVALID_ARGUMENT;
        auto renderer = gpu::render_device();
        if (!renderer) return ELG_GPU_IMAGE_DEVICE_UNAVAILABLE;
        auto external = std::make_shared<ExternalFrame>(input, renderer);
        std::uint32_t status;
        {
            std::lock_guard lock(renderer->mutex);
            if (!renderer->active) return ELG_GPU_IMAGE_DEVICE_UNAVAILABLE;
            status = external->import(input);
        }
        // Destruction may EndAccess, so it must happen OUTSIDE the renderer mutex, including
        // on failed imports that successfully began access before reporting a validation error.
        if (status != ELG_GPU_IMAGE_OK) return status;
        VideoFrame frame;
        frame.kind = VideoFrame::Kind::external;
        frame.width = input.width;
        frame.height = input.height;
        frame.source_id = id;
        frame.texture_id = next_external_id.fetch_add(1, std::memory_order_relaxed);
        frame.frame_id = frame.texture_id;
        frame.device = renderer->device;
        frame.texture = external->texture;
        frame.external_view = external->sample_view;
        frame.texture_lease = external;
        std::optional<VideoFrame> old;
        {
            std::lock_guard lock(mutex_);
            const Size next_size{static_cast<float>(input.width), static_cast<float>(input.height)};
            if (!size_task_pending_ && (natural_size_.width != next_size.width ||
                                       natural_size_.height != next_size.height)) {
                // Queue before accepting ownership so allocation failure leaves the old frame
                // intact. The task takes this mutex only after publication commits.
                post_ui_task([weak = std::weak_ptr<ExternalImageSource>(
                                  std::static_pointer_cast<ExternalImageSource>(shared_from_this()))] {
                    if (const auto source = weak.lock()) {
                        Size latest;
                        {
                            std::lock_guard source_lock(source->mutex_);
                            latest = source->natural_size_;
                            source->size_task_pending_ = false;
                        }
                        // Read the latest metadata, not captured dimensions or a GPU frame.
                        // A racing publication queues the next task; UI tasks run in order.
                        const auto previous = source->size.peek();
                        if (previous.width != latest.width || previous.height != latest.height)
                            source->size = latest;
                    }
                });
                size_task_pending_ = true;
            }
            external->accept();
            old.swap(frame_);
            frame_ = std::move(frame);
            natural_size_ = next_size;
            ++number_;
        }
        return ELG_GPU_IMAGE_OK; // old callbacks run outside the source lock
    }
    void clear() {
        std::optional<VideoFrame> old;
        {
            std::lock_guard lock(mutex_);
            old.swap(frame_);
            ++number_;
        }
    }
    bool update() override {
        std::lock_guard lock(mutex_);
        return std::exchange(reported_, number_) != number_;
    }
    std::optional<VideoFrame> current() const override {
        std::lock_guard lock(mutex_);
        return frame_; // strong snapshot reservation, not a weak owner notification
    }
    const bool flipped;
private:
    mutable std::mutex mutex_;
    Size natural_size_;
    bool size_task_pending_ = false;
    std::optional<VideoFrame> frame_;
    std::uint64_t number_ = 0, reported_ = 0;
};
} // namespace elg::detail

struct ElgGpuImage {
    std::shared_ptr<elg::detail::ExternalImageSource> source;
};

extern "C" {
ElgGpuImage* elg_gpu_image_create(uint32_t width, uint32_t height, uint32_t origin) {
    if (!width || !height || origin > ELG_GPU_IMAGE_BOTTOM_LEFT) return nullptr;
    try {
        return new ElgGpuImage{std::make_shared<elg::detail::ExternalImageSource>(
            width, height, origin == ELG_GPU_IMAGE_BOTTOM_LEFT)};
    } catch (...) { return nullptr; }
}
void elg_gpu_image_destroy(ElgGpuImage* image) { delete image; }
uint32_t elg_gpu_image_publish(ElgGpuImage* image, const ElgGpuImageFrame* frame) {
    if (!image || !frame) return ELG_GPU_IMAGE_INVALID_ARGUMENT;
    try { return image->source->publish(*frame); }
    catch (...) { return ELG_GPU_IMAGE_INTERNAL_ERROR; }
}
void elg_gpu_image_clear(ElgGpuImage* image) {
    if (image) image->source->clear();
}
}

namespace elg {
std::shared_ptr<detail::VideoSource> gpu_image_source(const ElgGpuImage* image) {
    return image ? image->source : nullptr;
}
bool gpu_image_flipped(const ElgGpuImage* image) {
    return image && image->source->flipped;
}
}
