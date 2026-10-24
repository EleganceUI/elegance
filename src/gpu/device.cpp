// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include "gpu/device.h"

#include "core/crash.h"
#include "core/log.h"
#include "platform/platform.h"

#include <elg/error.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <format>
#include <mutex>
#include <vector>

#if defined(_WIN32)
#include <windows.h> // the build defines WIN32_LEAN_AND_MEAN and NOMINMAX
#include <dxgi1_4.h>
#include <wrl/client.h>
#endif

namespace elg::detail::gpu {

struct Device::Errors {
    std::mutex mutex;
    std::string fatal; // device lost or out of memory: the next submit() throws it
};

namespace {

wgpu::BackendType requested_backend() {
    const std::string value = platform::environment_variable("ELG_GPU_BACKEND").value_or("");
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

#if defined(_WIN32)
// WebGPU doesn't report the adapter's LUID; DXGI does, for the adapter with the same IDs.
std::optional<std::array<std::uint8_t, 8>> find_luid(std::uint32_t vendor, std::uint32_t device) {
    Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
        return std::nullopt;
    }
    Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
    for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC1 desc{};
        if (SUCCEEDED(adapter->GetDesc1(&desc)) && desc.VendorId == vendor && desc.DeviceId == device) {
            std::array<std::uint8_t, 8> luid{};
            static_assert(sizeof(desc.AdapterLuid) == luid.size());
            std::memcpy(luid.data(), &desc.AdapterLuid, luid.size());
            return luid;
        }
    }
    return std::nullopt;
}

// Dawn's Direct3D 12 backend compiles shaders with the DirectX shader compiler, which it loads
// from beside the module Dawn is in (webgpu_dawn.dll, or the app itself when it links Dawn
// statically) or from System32. If it isn't there, creating the adapter fails instead of falling
// back to the older FXC, so ask for FXC up front.
bool dxc_available() {
    std::vector<std::filesystem::path> folders;
    HMODULE dawn = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&wgpuCreateInstance), &dawn)) {
        wchar_t path[MAX_PATH];
        const DWORD length = GetModuleFileNameW(dawn, path, MAX_PATH);
        if (length > 0 && length < MAX_PATH) {
            folders.push_back(std::filesystem::path(path).parent_path());
        }
    }
    wchar_t system[MAX_PATH];
    const UINT length = GetSystemDirectoryW(system, MAX_PATH);
    if (length > 0 && length < MAX_PATH) {
        folders.emplace_back(system);
    }
    std::error_code error;
    return std::ranges::any_of(folders, [&](const std::filesystem::path& folder) {
        return std::filesystem::exists(folder / L"dxcompiler.dll", error) &&
               std::filesystem::exists(folder / L"dxil.dll", error);
    });
}
#endif

} // namespace

Device::Device(const Instance& instance) : instance_(instance), errors_(std::make_unique<Errors>()) {
    wgpu::RequestAdapterOptions options;
    options.powerPreference = wgpu::PowerPreference::HighPerformance;
    options.backendType = requested_backend();
#if defined(_WIN32)
    const char* const no_dxc[] = {"use_dxc"};
    wgpu::DawnTogglesDescriptor toggles;
    if (!dxc_available()) {
        toggles.disabledToggleCount = 1;
        toggles.disabledToggles = no_dxc;
        options.nextInChain = &toggles;
        log_info("dxcompiler.dll and dxil.dll aren't beside the app: Direct3D 12 compiles shaders with FXC (slower)");
    }
#endif
    std::string adapter_error;
    instance.wait(instance.handle().RequestAdapter(
        &options, wgpu::CallbackMode::WaitAnyOnly,
        [&](wgpu::RequestAdapterStatus status, wgpu::Adapter adapter, wgpu::StringView message) {
            if (status == wgpu::RequestAdapterStatus::Success) {
                adapter_ = std::move(adapter);
            } else {
                adapter_error = to_string(message);
            }
        }));
    if (!adapter_) {
        throw GpuError(std::format("no GPU can run WebGPU{}{}",
                                   options.backendType == wgpu::BackendType::Undefined
                                       ? std::string()
                                       : std::format(" on {}", to_string(options.backendType)),
                                   adapter_error.empty() ? "" : ": " + adapter_error));
    }

    wgpu::AdapterInfo info;
    adapter_.GetInfo(&info);
    name_ = to_string(info.device);
    if (name_.empty()) {
        name_ = to_string(info.description);
    }
    backend_ = info.backendType;
#if defined(_WIN32)
    luid_ = find_luid(info.vendorID, info.deviceID);
#endif

    // Optional features: sharing textures with Direct3D 11 (video frames, composition).
    std::vector<wgpu::FeatureName> features;
    for (const wgpu::FeatureName feature :
         {wgpu::FeatureName::SharedTextureMemoryDXGISharedHandle, wgpu::FeatureName::SharedFenceDXGISharedHandle}) {
        if (adapter_.HasFeature(feature)) {
            features.push_back(feature);
        }
    }

    // As much as the GPU allows of what large images and video frames need.
    wgpu::Limits supported;
    adapter_.GetLimits(&supported);
    wgpu::Limits limits;
    limits.maxTextureDimension2D = supported.maxTextureDimension2D;
    limits.maxBufferSize = supported.maxBufferSize;
    limits.maxStorageBufferBindingSize = supported.maxStorageBufferBindingSize;

    wgpu::DeviceDescriptor descriptor;
    descriptor.label = "Elegence";
    descriptor.requiredFeatureCount = features.size();
    descriptor.requiredFeatures = features.data();
    descriptor.requiredLimits = &limits;
    descriptor.SetDeviceLostCallback(
        wgpu::CallbackMode::AllowSpontaneous,
        [errors = errors_.get()](const wgpu::Device&, wgpu::DeviceLostReason reason, wgpu::StringView message) {
            if (reason == wgpu::DeviceLostReason::Destroyed || reason == wgpu::DeviceLostReason::CallbackCancelled) {
                return; // our own shutdown
            }
            std::lock_guard lock(errors->mutex);
            errors->fatal = std::format("the GPU device was lost ({}); device loss recovery is not implemented yet",
                                        std::string_view(message));
        });
    descriptor.SetUncapturedErrorCallback(
        [](const wgpu::Device&, wgpu::ErrorType type, wgpu::StringView message, Errors* errors) {
            // A C callback: nothing may unwind through it.
            try {
                if (type == wgpu::ErrorType::OutOfMemory) {
                    std::lock_guard lock(errors->mutex);
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
            }
        },
        errors_.get());

    std::string device_error;
    instance.wait(adapter_.RequestDevice(
        &descriptor, wgpu::CallbackMode::WaitAnyOnly,
        [&](wgpu::RequestDeviceStatus status, wgpu::Device device, wgpu::StringView message) {
            if (status == wgpu::RequestDeviceStatus::Success) {
                device_ = std::move(device);
            } else {
                device_error = to_string(message);
            }
        }));
    if (!device_) {
        throw GpuError(std::format("the GPU \"{}\" could not create a WebGPU device: {}", name_, device_error));
    }
    queue_ = device_.GetQueue();

    device_.SetLoggingCallback([](wgpu::LoggingType type, wgpu::StringView message) {
        try {
            if (type == wgpu::LoggingType::Error) {
                log_error("webgpu: {}", std::string_view(message));
            } else if (type == wgpu::LoggingType::Warning) {
                log_warning("webgpu: {}", std::string_view(message));
            } else {
                log_debug("webgpu: {}", std::string_view(message));
            }
        } catch (...) {
        }
    });

    log_info("GPU: {} ({} through WebGPU)", name_, to_string(backend_));
}

Device::~Device() {
    if (device_) {
        device_.Destroy(); // finishes the GPU's work; the lost callback sees Destroyed
    }
}

void Device::check() const {
    std::lock_guard lock(errors_->mutex);
    if (!errors_->fatal.empty()) {
        throw GpuError(errors_->fatal);
    }
}

void Device::submit(const wgpu::CommandBuffer& commands) const {
    queue_.Submit(1, &commands);
    check();
}

void Device::wait_idle() const {
    instance_.wait(queue_.OnSubmittedWorkDone(wgpu::CallbackMode::WaitAnyOnly,
                                              [](wgpu::QueueWorkDoneStatus, wgpu::StringView) {}));
    check();
}

void Device::tick() const {
    instance_.handle().ProcessEvents();
    check();
}

} // namespace elg::detail::gpu
