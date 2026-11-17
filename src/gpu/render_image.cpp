// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include "gpu/render_image.h"

#include <webgpu/webgpu_cpp.h>

#include <algorithm>

namespace elg::detail::gpu {

wgpu::Texture create_render_texture(const wgpu::Device& device, wgpu::TextureFormat format, std::uint32_t width,
                                    std::uint32_t height, std::uint32_t samples, wgpu::TextureUsage usage) {
    wgpu::TextureDescriptor descriptor;
    descriptor.label = "offscreen image";
    descriptor.dimension = wgpu::TextureDimension::e2D;
    descriptor.size = {width, height, 1};
    descriptor.format = format;
    descriptor.sampleCount = samples;
    descriptor.usage = usage;
    return device.CreateTexture(&descriptor);
}

std::size_t image_bytes(wgpu::TextureFormat format, std::uint32_t width, std::uint32_t height, std::uint32_t samples) {
    std::size_t texel = 16; // formats not listed: at most this
    switch (format) {
    case wgpu::TextureFormat::R8Unorm: texel = 1; break;
    // Backends may keep stencil in a 32-bit depth-stencil format (D24S8 on Direct3D, and on
    // Vulkan without separate 8-bit stencil): counted that large everywhere.
    case wgpu::TextureFormat::Stencil8:
    case wgpu::TextureFormat::RGBA8Unorm:
    case wgpu::TextureFormat::RGBA8UnormSrgb:
    case wgpu::TextureFormat::BGRA8Unorm:
    case wgpu::TextureFormat::BGRA8UnormSrgb: texel = 4; break;
    default: break;
    }
    return texel * width * height * std::max(samples, 1u);
}

} // namespace elg::detail::gpu
