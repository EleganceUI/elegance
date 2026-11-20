// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include "gpu/instance.h"

#include "core/profile.h"
#include "gpu/system.h"
#include "gpu/webgpu.h"

#include <elg/error.h>

#include <webgpu/webgpu_cpp.h>

#include <cstdint>
#include <iterator>

namespace elg::detail::gpu {

Instance::Instance() : instance_(std::make_unique<wgpu::Instance>()) {
    // WaitAny with a timeout: requesting the adapter and device, and waiting for the GPU.
    const wgpu::InstanceFeatureName features[] = {wgpu::InstanceFeatureName::TimedWaitAny};
    wgpu::InstanceDescriptor descriptor;
    descriptor.requiredFeatureCount = std::size(features);
    descriptor.requiredFeatures = features;
    *instance_ = create_instance(descriptor);
    if (!*instance_) {
        throw GpuError("WebGPU is not available: the instance could not be created");
    }
}

void Instance::wait(wgpu::Future future) const {
    ELG_ZONE("Wait for GPU");
    constexpr std::uint64_t kTimeout = 5'000'000'000; // nanoseconds
    const wgpu::WaitStatus status = instance_->WaitAny(future, kTimeout);
    if (status == wgpu::WaitStatus::TimedOut) {
        throw DeviceLost("the GPU didn't answer for 5 seconds: the device is treated as lost");
    }
    if (status != wgpu::WaitStatus::Success) {
        throw_gpu_error("wait for the GPU");
    }
}

wgpu::Surface Instance::create_surface(const SurfaceSource& source) const {
    using Kind = SurfaceSource::Kind;
    wgpu::SurfaceSourceWindowsHWND win32;
    wgpu::SurfaceSourceMetalLayer metal;
    wgpu::SurfaceSourceXlibWindow xlib;
    wgpu::SurfaceSourceWaylandSurface wayland;
    wgpu::SurfaceDescriptor descriptor;
    switch (source.kind) {
    case Kind::win32:
        win32.hinstance = source.display;
        win32.hwnd = source.handle;
        descriptor.nextInChain = &win32;
        break;
    case Kind::metal:
        metal.layer = source.handle;
        descriptor.nextInChain = &metal;
        break;
    case Kind::xlib:
        xlib.display = source.display;
        xlib.window = source.xlib_window;
        descriptor.nextInChain = &xlib;
        break;
    case Kind::wayland:
        wayland.display = source.display;
        wayland.surface = source.handle;
        descriptor.nextInChain = &wayland;
        break;
    case Kind::none: throw GpuError("the window has nothing to draw into");
    }
    wgpu::Surface surface = instance_->CreateSurface(&descriptor);
    if (!surface) {
        throw_gpu_error("create a window surface");
    }
    return surface;
}

Instance::~Instance() = default;

const wgpu::Instance& Instance::handle() const noexcept {
    return *instance_;
}

} // namespace elg::detail::gpu
