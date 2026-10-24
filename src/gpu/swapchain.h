// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_GPU_SWAPCHAIN_H
#define ELG_GPU_SWAPCHAIN_H

#include "gpu/device.h"
#include "gpu/webgpu.h"

#include <cstdint>

namespace elg::detail::gpu {

// A window's surface, configured as a swapchain: 8-bit images rendered through sRGB views,
// presented with FIFO (vsync).
class Swapchain {
public:
    // `transparent`: the window shows through where the image's alpha is below 1 (needs a
    // compositor that blends; otherwise it stays opaque).
    Swapchain(const Device& device, wgpu::Surface surface, bool transparent = false);
    ~Swapchain();

    Swapchain(const Swapchain&) = delete;
    Swapchain& operator=(const Swapchain&) = delete;

    // (Re)configures the surface if it's stale or the window's size changed. Returns false when
    // the window has no area (e.g. minimized); there is nothing to present then.
    bool prepare(std::uint32_t width, std::uint32_t height);
    // The image to render the next frame into, or null when the surface must be reconfigured
    // first (skip the frame).
    wgpu::TextureView acquire();
    void present();

    // The format of the views acquire() returns (sRGB).
    wgpu::TextureFormat view_format() const noexcept { return view_format_; }
    std::uint32_t width() const noexcept { return width_; }
    std::uint32_t height() const noexcept { return height_; }

private:
    const Device& device_;
    wgpu::Surface surface_;
    wgpu::TextureFormat format_ = wgpu::TextureFormat::Undefined;
    wgpu::TextureFormat view_format_ = wgpu::TextureFormat::Undefined;
    wgpu::CompositeAlphaMode alpha_ = wgpu::CompositeAlphaMode::Auto;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    bool stale_ = true;
};

} // namespace elg::detail::gpu

#endif // ELG_GPU_SWAPCHAIN_H
