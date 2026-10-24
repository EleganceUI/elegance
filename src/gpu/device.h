// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_GPU_DEVICE_H
#define ELG_GPU_DEVICE_H

#include "gpu/instance.h"
#include "gpu/webgpu.h"

#include <array>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

namespace elg::detail::gpu {

// A retained snapshot of the renderer's exact Dawn device, not just its adapter.
// Lock mutex while checking active and creating producer resources; shutdown uses the same
// lock before destroying the device. If also locking a source, lock the source first; shutdown
// never locks sources. Device/queue references are immutable after publication; only active
// changes. Retaining this keeps handles alive, not the renderer.
struct RenderDevice {
    std::mutex mutex;
    bool active = true;
    wgpu::Device device;
    wgpu::Queue queue;
};
std::shared_ptr<RenderDevice> render_device();

// The adapter (GPU), the device and its queue. Owned by the render thread.
// ELG_GPU_BACKEND=d3d12|d3d11|vulkan|metal|opengl picks the API Dawn runs on; by default it picks
// the best one the platform has (Direct3D 12 on Windows, Metal on macOS, Vulkan on Linux).
class Device {
public:
    explicit Device(const Instance& instance);
    ~Device();

    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;

    const wgpu::Device& handle() const noexcept { return device_; }
    const wgpu::Adapter& adapter() const noexcept { return adapter_; }
    const wgpu::Queue& queue() const noexcept { return queue_; }
    const Instance& instance() const noexcept { return instance_; }
    const std::string& name() const noexcept { return name_; }
    wgpu::BackendType backend() const noexcept { return backend_; }

    // Whether the device was created with this (optional) feature.
    bool has_feature(wgpu::FeatureName feature) const { return device_.HasFeature(feature); }
    // The adapter's LUID on Windows: Direct3D devices that share textures with this one (video
    // decoding, composition) must run on the same adapter.
    std::optional<std::array<std::uint8_t, 8>> luid() const noexcept { return luid_; }

    // Submits `commands`, then throws elg::GpuError if the device was lost or ran out of memory.
    void submit(const wgpu::CommandBuffer& commands) const;
    // Blocks until everything submitted so far has finished.
    void wait_idle() const;
    // Runs pending callbacks and lets Dawn release finished work. Once per frame.
    void tick() const;

private:
    // Errors reported by callbacks, which may run on Dawn's threads: checked by submit().
    struct Errors;
    void check() const;

    const Instance& instance_;
    std::shared_ptr<Errors> errors_;
    wgpu::Adapter adapter_;
    wgpu::Device device_;
    wgpu::Queue queue_;
    std::shared_ptr<RenderDevice> published_;
    std::string name_;
    wgpu::BackendType backend_ = wgpu::BackendType::Undefined;
    std::optional<std::array<std::uint8_t, 8>> luid_;
};

} // namespace elg::detail::gpu

#endif // ELG_GPU_DEVICE_H
