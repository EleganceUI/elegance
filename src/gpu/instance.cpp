// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include "gpu/instance.h"

#include <elg/error.h>

#include <limits>
#include <span>
#include <string>
#include <string_view>

#if defined(_WIN32)
#include <dawn/native/DawnNative.h>
#include <windows.h>
#endif

namespace elg::detail::gpu {

Instance::Instance() {
    // WaitAny with a timeout: requesting the adapter and device, and waiting for the GPU.
    const wgpu::InstanceFeatureName features[] = {wgpu::InstanceFeatureName::TimedWaitAny};
    wgpu::InstanceDescriptor descriptor;
    descriptor.requiredFeatureCount = std::size(features);
    descriptor.requiredFeatures = features;
#if defined(_WIN32)
    // Dawn looks for d3dcompiler_47.dll (Direct3D 11) and vulkan-1.dll next to itself, then by
    // bare name. That second try uses LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR, which needs an absolute
    // path, so it always fails (ERROR_INVALID_PARAMETER) and the system's copies are never found.
    // Name System32 as a search path instead (Dawn appends the file name).
    char system[MAX_PATH];
    const UINT length = GetSystemDirectoryA(system, MAX_PATH);
    std::string system_dir = length > 0 && length < MAX_PATH ? std::string(system, length) + '\\' : std::string();
    std::string_view search_paths[] = {system_dir};
    dawn::native::DawnInstanceDescriptor dawn_descriptor;
    dawn_descriptor.additionalRuntimeSearchPaths = std::span(search_paths, system_dir.empty() ? 0 : 1);
    descriptor.nextInChain = &dawn_descriptor;
#endif
    instance_ = wgpu::CreateInstance(&descriptor);
    if (!instance_) {
        throw GpuError("WebGPU is not available: the instance could not be created");
    }
}

void Instance::wait(wgpu::Future future) const {
    if (instance_.WaitAny(future, std::numeric_limits<std::uint64_t>::max()) != wgpu::WaitStatus::Success) {
        throw_gpu_error("wait for the GPU");
    }
}

wgpu::Surface Instance::create_surface(const platform::SurfaceSource& source) const {
    using Kind = platform::SurfaceSource::Kind;
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
    wgpu::Surface surface = instance_.CreateSurface(&descriptor);
    if (!surface) {
        throw_gpu_error("create a window surface");
    }
    return surface;
}

} // namespace elg::detail::gpu
