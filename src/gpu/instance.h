// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_GPU_INSTANCE_H
#define ELG_GPU_INSTANCE_H

#include "gpu/webgpu.h"
#include "platform/generic/platform.h"

namespace elg::detail::gpu {

// The WebGPU instance. Lives on the render thread, which creates every GPU object, window
// surfaces included (from handles the UI thread's windows hand over, design.md §15).
class Instance {
public:
    Instance();

    Instance(const Instance&) = delete;
    Instance& operator=(const Instance&) = delete;

    const wgpu::Instance& handle() const noexcept { return instance_; }

    // Blocks until `future` (from a WaitAnyOnly callback) has completed.
    void wait(wgpu::Future future) const;

    // A surface for a window. The window must outlive it.
    wgpu::Surface create_surface(const platform::SurfaceSource& source) const;

private:
    wgpu::Instance instance_;
};

} // namespace elg::detail::gpu

#endif // ELG_GPU_INSTANCE_H
