// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_GPU_RENDER_IMAGE_H
#define ELG_GPU_RENDER_IMAGE_H

#include "gpu/webgpu.h"

#include <cstdint>

namespace elg::detail::gpu {

// An offscreen image that is rendered to and then sampled (layers, blur passes, path masks), or
// a multisampled / stencil scratch attachment (path masks).
class RenderImage {
public:
    RenderImage(const wgpu::Device& device, wgpu::TextureFormat format, std::uint32_t width, std::uint32_t height,
                std::uint32_t samples = 1,
                wgpu::TextureUsage usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding);

    const wgpu::Texture& texture() const noexcept { return texture_; }
    const wgpu::TextureView& view() const noexcept { return view_; }
    wgpu::TextureFormat format() const noexcept { return format_; }
    std::uint32_t width() const noexcept { return width_; }
    std::uint32_t height() const noexcept { return height_; }

private:
    wgpu::TextureFormat format_;
    std::uint32_t width_;
    std::uint32_t height_;
    wgpu::Texture texture_;
    wgpu::TextureView view_;
};

} // namespace elg::detail::gpu

#endif // ELG_GPU_RENDER_IMAGE_H
