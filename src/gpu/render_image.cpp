// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include "gpu/render_image.h"

namespace elg::detail::gpu {

RenderImage::RenderImage(const wgpu::Device& device, wgpu::TextureFormat format, std::uint32_t width,
                         std::uint32_t height, std::uint32_t samples, wgpu::TextureUsage usage)
    : format_(format), width_(width), height_(height) {
    wgpu::TextureDescriptor descriptor;
    descriptor.label = "offscreen image";
    descriptor.dimension = wgpu::TextureDimension::e2D;
    descriptor.size = {width, height, 1};
    descriptor.format = format;
    descriptor.sampleCount = samples;
    descriptor.usage = usage;
    texture_ = device.CreateTexture(&descriptor);
    view_ = texture_.CreateView();
}

} // namespace elg::detail::gpu
