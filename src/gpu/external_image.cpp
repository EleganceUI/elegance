// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include "gpu/external_image.h"

#include "core/profile.h"
#include "gpu/render_device.h"

#include "core/threads.h"

#include <elg/gpu_image.h>

#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace elg::detail::gpu {
namespace {
constexpr std::uint64_t kRetirementWaitNs = 5'000'000'000ull;

// EndAccess can return a Metal scheduling future. Waiting on it from a render-side lease
// destructor can stall the very thread that must keep submitting/ticking. One worker retains
// imported objects and callback state until retirement finishes, without blocking render/UI.
class RetirementWorker {
public:
    RetirementWorker() : thread_([this] { run(); }) {}
    RetirementWorker(const RetirementWorker&) = delete;
    RetirementWorker(RetirementWorker&&) = delete;
    RetirementWorker& operator=(const RetirementWorker&) = delete;
    RetirementWorker& operator=(RetirementWorker&&) = delete;
    ~RetirementWorker() {
        {
            const std::lock_guard lock(mutex_);
            stopping_ = true;
        }
        wake_.notify_one();
        thread_.join();
    }
    void enqueue(std::function<void()> job) {
        {
            const std::lock_guard lock(mutex_);
            jobs_.push_back(std::move(job));
        }
        wake_.notify_one();
    }
private:
    void run() {
        ELG_THREAD_NAME("Elegance GPU retirement");
        const ThreadSetup thread_setup; // retiring an image may release Direct3D objects (COM)
        for (;;) {
            std::function<void()> job;
            {
                std::unique_lock lock(mutex_);
                wake_.wait(lock, [this] { return stopping_ || !jobs_.empty(); });
                if (jobs_.empty()) {
                    return;
                }
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

// A popped validation scope's outcome, which the device reports later: wait() for it with no
// lock held. Empty (no scope was pushed): valid.
class PendingValidation {
public:
    PendingValidation() = default;
    PendingValidation(wgpu::Instance instance, wgpu::Future future, std::shared_ptr<bool> valid)
        : instance_(std::move(instance)), future_(future), valid_(std::move(valid)) {}
    [[nodiscard]] bool wait() const {
        return !valid_ || (instance_.WaitAny(future_, kRetirementWaitNs) == wgpu::WaitStatus::Success && *valid_);
    }
private:
    wgpu::Instance instance_;
    wgpu::Future future_{};
    std::shared_ptr<bool> valid_;
};

// Native import validation is a caller error, not an uncaptured renderer error. Keep the
// instance alive with the device so retirement can also capture errors after views disappear.
class ValidationScope {
public:
    explicit ValidationScope(const RenderDevice& renderer) : renderer_(renderer) {
        renderer_.device.PushErrorScope(wgpu::ErrorFilter::Validation);
    }
    ValidationScope(const ValidationScope&) = delete;
    ValidationScope(ValidationScope&&) = delete;
    ValidationScope& operator=(const ValidationScope&) = delete;
    ValidationScope& operator=(ValidationScope&&) = delete;
    ~ValidationScope() {
        if (!popped_) {
            renderer_.device.PopErrorScope(wgpu::CallbackMode::AllowSpontaneous,
                [](wgpu::PopErrorScopeStatus, wgpu::ErrorType, wgpu::StringView) {});
        }
    }
    // Pops the scope without waiting for its outcome.
    PendingValidation pop() {
        auto valid = std::make_shared<bool>(false);
        const auto future = renderer_.device.PopErrorScope(wgpu::CallbackMode::WaitAnyOnly,
            [valid](wgpu::PopErrorScopeStatus status, wgpu::ErrorType error, wgpu::StringView) {
                *valid = status == wgpu::PopErrorScopeStatus::Success && error == wgpu::ErrorType::NoError;
            });
        popped_ = true;
        return {renderer_.instance, future, std::move(valid)};
    }
    bool finish() { return pop().wait(); }
private:
    const RenderDevice& renderer_;
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

class ExternalFrame final : public ExternalImage {
public:
    ExternalFrame(ElgGpuImageFrame descriptor, std::shared_ptr<RenderDevice> renderer)
        : descriptor_(descriptor), renderer_(std::move(renderer)) {
        // This pointer and its chain are only valid for the synchronous import call.
        descriptor_.vk_image_create_info = nullptr;
    }
    ExternalFrame(const ExternalFrame&) = delete;
    ExternalFrame(ExternalFrame&&) = delete;
    ExternalFrame& operator=(const ExternalFrame&) = delete;
    ExternalFrame& operator=(ExternalFrame&&) = delete;
    ~ExternalFrame() override {
        if (!begun_) {
            return;
        }
        try {
            retirement_worker().enqueue([descriptor = descriptor_, renderer = renderer_,
                                         memory = memory_, image = texture_, view = view_,
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
            try {
                descriptor.release(descriptor.userdata, &result);
            } catch (...) {
                // Swallowed: a foreign callback is forbidden to unwind. Even a violating C++
                // callback must not cause a second ownership-transfer callback from here.
            }
        }
    }
    static void retire(const ElgGpuImageFrame& descriptor_,
                       const std::shared_ptr<RenderDevice>& renderer_,
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
                const std::lock_guard lock(renderer_->mutex);
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

    // Imports `frame` and begins access to it. The validation of what it did is left in
    // `validation`, for the caller to wait for once it holds no lock.
    std::uint32_t import(const ElgGpuImageFrame& frame, PendingValidation& validation) {
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
        // No texture longer than the device allows.
        if (frame.width > renderer_->max_texture_side || frame.height > renderer_->max_texture_side) {
            return ELG_GPU_IMAGE_IMPORT_FAILED;
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
            texture_ = memory_.CreateTexture(&desc);
            wgpu::TextureViewDescriptor view;
            view.format = sampled;
            view.usage = wgpu::TextureUsage::TextureBinding;
            view_ = texture_.CreateView(&view);
            ok = texture_ && view_;
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
            if (frame.transport == ELG_GPU_IMAGE_VULKAN_OPAQUE_FD) {
                access.nextInChain = &vk;
            }
            begun_ = memory_.BeginAccess(texture_, &access) == wgpu::Status::Success;
            ok = begun_;
        }
        validation = scope.pop();
        return ok ? ELG_GPU_IMAGE_OK : ELG_GPU_IMAGE_IMPORT_FAILED;
    }
    void accept() noexcept override { accepted_ = true; }
    [[nodiscard]] const wgpu::Device& device() const noexcept override { return renderer_->device; }
    [[nodiscard]] const wgpu::Texture& texture() const noexcept override { return texture_; }
    [[nodiscard]] const wgpu::TextureView& view() const noexcept override { return view_; }
private:
    ElgGpuImageFrame descriptor_;
    std::shared_ptr<RenderDevice> renderer_;
    wgpu::SharedTextureMemory memory_;
    wgpu::Texture texture_;
    wgpu::TextureView view_;
    bool begun_ = false;
    bool accepted_ = false;
};
} // namespace

std::uint32_t import_external_image(const ElgGpuImageFrame& frame, std::shared_ptr<ExternalImage>& image) {
    auto renderer = render_device();
    if (!renderer) {
        return ELG_GPU_IMAGE_DEVICE_UNAVAILABLE;
    }
    auto external = std::make_shared<ExternalFrame>(frame, renderer);
    std::uint32_t status = ELG_GPU_IMAGE_INTERNAL_ERROR;
    PendingValidation validation;
    {
        const std::lock_guard lock(renderer->mutex);
        if (!renderer->active) {
            return ELG_GPU_IMAGE_DEVICE_UNAVAILABLE;
        }
        status = external->import(frame, validation);
    }
    // The device reports validation errors asynchronously: waited for with the renderer
    // mutex released, since the render and UI threads take it.
    if (!validation.wait() && status == ELG_GPU_IMAGE_OK) {
        status = ELG_GPU_IMAGE_IMPORT_FAILED;
    }
    // Destruction may EndAccess, so it must happen OUTSIDE the renderer mutex, including
    // on failed imports that successfully began access before reporting a validation error.
    if (status == ELG_GPU_IMAGE_OK) {
        image = std::move(external);
    }
    return status;
}

bool in_current_device(const ExternalImage& image) {
    const std::shared_ptr<RenderDevice> device = render_device();
    if (!device) {
        return false;
    }
    const std::lock_guard lock(device->mutex);
    return device->active && device->device.Get() == image.device().Get();
}

} // namespace elg::detail::gpu
