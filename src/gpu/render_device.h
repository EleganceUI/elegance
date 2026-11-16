// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_GPU_RENDER_DEVICE_H
#define ELG_GPU_RENDER_DEVICE_H

#include <webgpu/webgpu_cpp.h>

#include <cstdint>
#include <memory>
#include <mutex>

namespace elg::detail::gpu {

// A retained snapshot of the renderer's exact Dawn device, not just its adapter.
// Lock mutex while checking active and creating producer resources; shutdown uses the same
// lock to stop publication. If also locking a source, lock the source first; shutdown never
// locks sources. Device/queue references are immutable after publication; only active changes.
// Retaining this keeps the device/instance usable for accepted-frame retirement, not rendering.
struct RenderDevice {
    std::mutex mutex;
    bool active = true;
    wgpu::Device device;
    wgpu::Queue queue;
    wgpu::Instance instance; // external-image error scopes and Metal scheduling at retirement
    wgpu::BackendType backend = wgpu::BackendType::Undefined;
    std::uint32_t max_texture_side = 0; // Device::max_texture_side()
};
// The device the running renderer published (gpu/device.h), or null.
std::shared_ptr<RenderDevice> render_device();

} // namespace elg::detail::gpu

#endif // ELG_GPU_RENDER_DEVICE_H
