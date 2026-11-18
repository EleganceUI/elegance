// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_GPU_DEVICE_H
#define ELG_GPU_DEVICE_H

#include "gpu/webgpu_types.h"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace elg::detail::gpu {

class Instance;
struct RenderDevice; // gpu/render_device.h

// The adapter (GPU), the device and its queue. Owned by the render thread.
// ELG_GPU_BACKEND=d3d12|d3d11|vulkan|metal|opengl picks the API Dawn runs on; by default it picks
// the best one the platform has (Direct3D 12 on Windows, Metal on macOS, Vulkan on Linux). While
// it lives, it's published as render_device().
class Device {
public:
    explicit Device(const Instance& instance);
    ~Device();

    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;
    Device(Device&&) = delete;
    Device& operator=(Device&&) = delete;

    [[nodiscard]] const wgpu::Device& handle() const noexcept;
    [[nodiscard]] const wgpu::Adapter& adapter() const noexcept;
    [[nodiscard]] const wgpu::Queue& queue() const noexcept;
    [[nodiscard]] const Instance& instance() const noexcept { return instance_; }
    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    [[nodiscard]] wgpu::BackendType backend() const noexcept { return backend_; }

    // The longest side a 2D texture can have on this device, in texels.
    [[nodiscard]] std::uint32_t max_texture_side() const noexcept { return max_texture_side_; }

    // Whether the device was created with this (optional) feature.
    [[nodiscard]] bool has_feature(wgpu::FeatureName feature) const;
    // The adapter's LUID on Windows: Direct3D devices that share textures with this one (video
    // decoding, composition) must run on the same adapter.
    [[nodiscard]] std::optional<std::array<std::uint8_t, 8>> luid() const noexcept { return luid_; }

    // Submits `commands`, then throws DeviceLost if the device was lost, or elg::GpuError if it ran
    // out of memory.
    void submit(const wgpu::CommandBuffer& commands) const;
    // Throws DeviceLost if the device was lost, or elg::GpuError if it ran out of memory.
    void check() const;
    // Blocks until everything submitted so far has finished.
    void wait_idle() const;
    // Runs pending callbacks and lets Dawn release finished work. Once per frame.
    void tick() const;
    // Blocks until Dawn has destroyed what it deferred until the GPU is past it (swapchains,
    // say), which it otherwise may do no sooner than the device's destruction.
    void finish_deferred() const;

private:
    // The adapter, the device and its queue.
    struct Objects;
    // Errors reported by callbacks, which may run on Dawn's threads: checked by submit().
    struct Errors;

    const Instance& instance_;
    std::shared_ptr<Errors> errors_;
    std::unique_ptr<Objects> objects_;
    std::shared_ptr<RenderDevice> published_;
    std::string name_;
    wgpu::BackendType backend_{}; // Undefined until the adapter is found
    std::optional<std::array<std::uint8_t, 8>> luid_;
    std::uint32_t max_texture_side_ = 0;
};

} // namespace elg::detail::gpu

#endif // ELG_GPU_DEVICE_H
