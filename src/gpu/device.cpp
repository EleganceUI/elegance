// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include "gpu/device.h"

#ifndef NDEBUG
#include "core/crash.h"
#endif
#include "core/log.h"
#include "core/os_backend.h"
#include "gpu/instance.h"
#include "gpu/render_device.h"
#include "gpu/system.h"
#include "gpu/webgpu.h"

#include <elg/error.h>

#include <webgpu/webgpu_cpp.h>

#include <format>
#include <mutex>
#include <span>
#include <unordered_map>
#include <vector>

namespace elg::detail::gpu {

struct Device::Objects {
    wgpu::Adapter adapter;
    wgpu::Device device;
    wgpu::Queue queue;
};

static_assert(wgpu::BackendType{} == wgpu::BackendType::Undefined); // Device::backend_'s first value

struct Device::Errors {
    std::mutex mutex;
    std::string fatal; // device lost or out of memory: the next submit() throws it

    // Dawn's repeatable uncaptured-error callback cannot own a capturing lambda. Look up a
    // shared state by the callback's device instead of passing a dangling Errors* as userdata.
    // Unregistration prevents late reports from retained native handles touching a dead Device;
    // a callback already in progress retains its own reference through completion.
    struct Registry {
        std::mutex mutex;
        std::unordered_map<WGPUDevice, std::weak_ptr<Errors>> devices;
    };
    static Registry& registry() {
        // Retained Dawn handles can report errors during static teardown. Keep the small
        // registry/mutex alive for the process; per-device entries are removed on shutdown.
        static auto* const registry = new Registry;
        return *registry;
    }
    static std::shared_ptr<Errors> find(WGPUDevice device) {
        auto& states = registry();
        const std::lock_guard lock(states.mutex);
        const auto it = states.devices.find(device);
        return it == states.devices.end() ? nullptr : it->second.lock();
    }
};

namespace {

std::mutex g_render_device_mutex;
std::weak_ptr<RenderDevice> g_render_device;

wgpu::BackendType requested_backend() {
    const std::string value = os::environment_variable("ELG_GPU_BACKEND").value_or("");
    if (value.empty()) {
        return wgpu::BackendType::Undefined;
    }
    constexpr std::pair<std::string_view, wgpu::BackendType> kBackends[] = {
        {"d3d12", wgpu::BackendType::D3D12},   {"d3d11", wgpu::BackendType::D3D11},
        {"vulkan", wgpu::BackendType::Vulkan}, {"metal", wgpu::BackendType::Metal},
        {"opengl", wgpu::BackendType::OpenGL}, {"opengles", wgpu::BackendType::OpenGLES},
    };
    for (const auto& [name, backend] : kBackends) {
        if (value == name) {
            return backend;
        }
    }
    log_warning("ELG_GPU_BACKEND={} is not recognized (use d3d12, d3d11, vulkan, metal, opengl or opengles)", value);
    return wgpu::BackendType::Undefined;
}

} // namespace

std::shared_ptr<RenderDevice> render_device() {
    const std::lock_guard lock(g_render_device_mutex);
    return g_render_device.lock();
}

Device::Device(const Instance& instance)
    : instance_(instance), errors_(std::make_shared<Errors>()), objects_(std::make_unique<Objects>()) {
    wgpu::RequestAdapterOptions options;
    options.powerPreference = wgpu::PowerPreference::HighPerformance;
    options.backendType = requested_backend();
    const std::span<const char* const> disabled = disabled_adapter_toggles();
    wgpu::DawnTogglesDescriptor toggles;
    if (!disabled.empty()) {
        toggles.disabledToggleCount = disabled.size();
        toggles.disabledToggles = disabled.data();
        options.nextInChain = &toggles;
    }
    std::string adapter_error;
    instance.wait(instance.handle().RequestAdapter(
        &options, wgpu::CallbackMode::WaitAnyOnly,
        [&](wgpu::RequestAdapterStatus status, wgpu::Adapter adapter, wgpu::StringView message) {
            if (status == wgpu::RequestAdapterStatus::Success) {
                objects_->adapter = std::move(adapter);
            } else {
                adapter_error = to_string(message);
            }
        }));
    if (!objects_->adapter) {
        throw GpuError(std::format("no GPU can run WebGPU{}{}",
                                   options.backendType == wgpu::BackendType::Undefined
                                       ? std::string()
                                       : std::format(" on {}", to_string(options.backendType)),
                                   adapter_error.empty() ? "" : ": " + adapter_error));
    }

    wgpu::AdapterInfo info;
    objects_->adapter.GetInfo(&info);
    name_ = to_string(info.device);
    if (name_.empty()) {
        name_ = to_string(info.description);
    }
    backend_ = info.backendType;
    luid_ = adapter_luid(info.vendorID, info.deviceID);

    // External-image publication imports resources from producer threads. Dawn's implicit
    // synchronization keeps those operations safe alongside rendering and frame retirement.
    if (!objects_->adapter.HasFeature(wgpu::FeatureName::ImplicitDeviceSynchronization)) {
        throw GpuError("Dawn must support ImplicitDeviceSynchronization for external-image publication");
    }
    std::vector<wgpu::FeatureName> features{wgpu::FeatureName::ImplicitDeviceSynchronization};
    // Optional sharing features. Never require unavailable transports just to start the
    // renderer: GpuImage reports Unsupported on the actual device at publication.
    for (const wgpu::FeatureName feature :
         {wgpu::FeatureName::SharedTextureMemoryDXGISharedHandle, wgpu::FeatureName::SharedFenceDXGISharedHandle,
                   wgpu::FeatureName::SharedTextureMemoryIOSurface, wgpu::FeatureName::SharedFenceMTLSharedEvent,
                   wgpu::FeatureName::SharedTextureMemoryOpaqueFD, wgpu::FeatureName::SharedFenceVkSemaphoreOpaqueFD,
                   wgpu::FeatureName::SharedTextureMemoryVkDedicatedAllocation}) {
        if (objects_->adapter.HasFeature(feature)) {
            features.push_back(feature);
        }
    }

    // As much as the GPU allows of what large images and video frames need.
    wgpu::Limits supported;
    objects_->adapter.GetLimits(&supported);
    wgpu::Limits limits;
    limits.maxTextureDimension2D = supported.maxTextureDimension2D;
    limits.maxBufferSize = supported.maxBufferSize;
    limits.maxStorageBufferBindingSize = supported.maxStorageBufferBindingSize;

    wgpu::DeviceDescriptor descriptor;
    descriptor.label = "Elegance";
    descriptor.requiredFeatureCount = features.size();
    descriptor.requiredFeatures = features.data();
    descriptor.requiredLimits = &limits;
    // ELG_GPU_LABELS=1: our labels reach the backend's objects, so GPU profilers (Instruments,
    // PIX, RenderDoc) name each render pass ("blur", "layer shadow", "window", ...).
    const char* const labels[] = {"use_user_defined_labels_in_backend"};
    wgpu::DawnTogglesDescriptor device_toggles;
    if (os::environment_variable("ELG_GPU_LABELS").value_or("") == "1") {
        device_toggles.enabledToggleCount = 1;
        device_toggles.enabledToggles = labels;
        descriptor.nextInChain = &device_toggles;
        log_info("GPU objects carry their labels in the backend (ELG_GPU_LABELS)");
    }
    descriptor.SetDeviceLostCallback(
        wgpu::CallbackMode::AllowSpontaneous,
        [errors = errors_](const wgpu::Device&, wgpu::DeviceLostReason reason, wgpu::StringView message) {
            try {
                if (reason == wgpu::DeviceLostReason::Destroyed || reason == wgpu::DeviceLostReason::CallbackCancelled) {
                    return; // our own shutdown
                }
                const std::lock_guard lock(errors->mutex);
                errors->fatal = std::format("the GPU device was lost ({}); device loss recovery is not implemented yet",
                                            std::string_view(message));
            } catch (...) {
                // Swallowed: a C callback; even allocation failures must not unwind through Dawn.
            }
        });
    descriptor.SetUncapturedErrorCallback(
        [](const wgpu::Device& device, wgpu::ErrorType type, wgpu::StringView message) {
            // A C callback: nothing may unwind through it.
            try {
                const auto errors = Errors::find(device.Get());
                if (!errors) {
                    return; // not published yet, or the renderer has shut down
                }
                if (type == wgpu::ErrorType::OutOfMemory) {
                    const std::lock_guard lock(errors->mutex);
                    errors->fatal = std::format("the GPU ran out of memory: {}", std::string_view(message));
                    return;
                }
#ifdef NDEBUG
                log_error("webgpu: {}", std::string_view(message));
#else
                // Every GPU object belongs to the framework, so a validation error is our bug.
                internal_error(std::format("WebGPU error: {}", std::string_view(message)));
#endif
            } catch (...) {
                // Swallowed: a C callback; nothing may unwind through Dawn.
            }
        });

    std::string device_error;
    instance.wait(objects_->adapter.RequestDevice(
        &descriptor, wgpu::CallbackMode::WaitAnyOnly,
        [&](wgpu::RequestDeviceStatus status, wgpu::Device device, wgpu::StringView message) {
            if (status == wgpu::RequestDeviceStatus::Success) {
                objects_->device = std::move(device);
            } else {
                device_error = to_string(message);
            }
        }));
    if (!objects_->device) {
        throw GpuError(std::format("the GPU \"{}\" could not create a WebGPU device: {}", name_, device_error));
    }
    objects_->queue = objects_->device.GetQueue();
    wgpu::Limits granted;
    objects_->device.GetLimits(&granted);
    max_texture_side_ = granted.maxTextureDimension2D;

    objects_->device.SetLoggingCallback([](wgpu::LoggingType type, wgpu::StringView message) {
        try {
            if (type == wgpu::LoggingType::Error) {
                log_error("webgpu: {}", std::string_view(message));
            } else if (type == wgpu::LoggingType::Warning) {
                log_warning("webgpu: {}", std::string_view(message));
            } else {
                log_debug("webgpu: {}", std::string_view(message));
            }
        } catch (...) {
            // Swallowed: a C callback; nothing may unwind through Dawn.
        }
    });

    log_info("GPU: {} ({} through WebGPU)", name_, to_string(backend_));

    published_ = std::make_shared<RenderDevice>();
    published_->device = objects_->device;
    published_->queue = objects_->queue;
    published_->instance = instance.handle();
    published_->backend = backend_;
    published_->max_texture_side = max_texture_side_;
    {
        auto& states = Errors::registry();
        const std::lock_guard lock(states.mutex);
        // Also prune expired entries from constructors that failed after registration.
        std::erase_if(states.devices, [](const auto& entry) { return entry.second.expired(); });
        states.devices[objects_->device.Get()] = errors_;
    }
    const std::lock_guard lock(g_render_device_mutex);
    g_render_device = published_;
}

Device::~Device() {
    // Stop new publications, but let accepted external frames finish EndAccess using
    // their retained device/instance after the render thread has stopped.
    {
        const std::lock_guard lock(g_render_device_mutex);
        if (g_render_device.lock() == published_) {
            g_render_device.reset();
        }
    }
    {
        auto& states = Errors::registry();
        const std::lock_guard lock(states.mutex);
        states.devices.erase(objects_->device.Get());
    }
    const std::lock_guard lock(published_->mutex);
    published_->active = false;
    // Do not eagerly Destroy(): imported frames may still need to export completion
    // fences. Dawn references held by those frames and submitted work own the remaining
    // lifetime; releasing the renderer's references is not a GPU-completion assertion.
}

const wgpu::Device& Device::handle() const noexcept {
    return objects_->device;
}

const wgpu::Adapter& Device::adapter() const noexcept {
    return objects_->adapter;
}

const wgpu::Queue& Device::queue() const noexcept {
    return objects_->queue;
}

bool Device::has_feature(wgpu::FeatureName feature) const {
    return objects_->device.HasFeature(feature);
}

void Device::check() const {
    const std::lock_guard lock(errors_->mutex);
    if (!errors_->fatal.empty()) {
        throw GpuError(errors_->fatal);
    }
}

void Device::submit(const wgpu::CommandBuffer& commands) const {
    objects_->queue.Submit(1, &commands);
    check();
}

void Device::wait_idle() const {
    instance_.wait(objects_->queue.OnSubmittedWorkDone(wgpu::CallbackMode::WaitAnyOnly,
                                                       [](wgpu::QueueWorkDoneStatus, wgpu::StringView) {}));
    check();
}

void Device::tick() const {
    instance_.handle().ProcessEvents();
    check();
}

void Device::finish_deferred() const {
    // Deferred deletions wait for the next submit, and Dawn skips submits that record nothing.
    wgpu::BufferDescriptor descriptor;
    descriptor.size = 4;
    descriptor.usage = wgpu::BufferUsage::CopyDst;
    const wgpu::Buffer scrap = objects_->device.CreateBuffer(&descriptor);
    const auto submit_scrap = [&] {
        const wgpu::CommandEncoder encoder = objects_->device.CreateCommandEncoder();
        encoder.ClearBuffer(scrap);
        submit(encoder.Finish());
    };
    submit_scrap();
    wait_idle();
    // Waiting doesn't run the deletions; Tick does, but only while work is outstanding.
    submit_scrap();
    objects_->device.Tick();
    check();
}

} // namespace elg::detail::gpu
