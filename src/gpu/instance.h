// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_GPU_INSTANCE_H
#define ELG_GPU_INSTANCE_H

#include "gpu/webgpu_types.h"
#include "gpu/surface_source.h"

#include <elg/error.h>

#include <memory>

namespace elg::detail::gpu {

// The GPU device was lost, or didn't answer for 5 seconds, which counts as lost (ELGSPEC-0002.R12).
// The renderer then starts over on a new device (ELGSPEC-0025.R4).
class DeviceLost : public GpuError {
public:
    using GpuError::GpuError;
};

// The WebGPU instance. Lives on the render thread, which creates every GPU object, window
// surfaces included (from handles the UI thread's windows hand over, design.md §15).
class Instance {
public:
    Instance();
    ~Instance();

    Instance(const Instance&) = delete;
    Instance& operator=(const Instance&) = delete;
    Instance(Instance&&) = delete;
    Instance& operator=(Instance&&) = delete;

    [[nodiscard]] const wgpu::Instance& handle() const noexcept;

    // Blocks until `future` (from a WaitAnyOnly callback) has completed, for at most 5 seconds:
    // after that, throws DeviceLost.
    void wait(wgpu::Future future) const;

    // A surface for a window. The window must outlive it.
    [[nodiscard]] wgpu::Surface create_surface(const SurfaceSource& source) const;

private:
    std::unique_ptr<wgpu::Instance> instance_;
};

} // namespace elg::detail::gpu

#endif // ELG_GPU_INSTANCE_H
