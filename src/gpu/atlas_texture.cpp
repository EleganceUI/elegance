// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include "gpu/atlas_texture.h"

#include "gpu/device.h"
#include "gpu/webgpu.h"

#include <webgpu/webgpu_cpp.h>

namespace elg::detail::gpu {

struct AtlasTexture::Objects {
    wgpu::Texture texture;
    wgpu::BindGroup group;
};

AtlasTexture::AtlasTexture(const Device& device, const wgpu::BindGroupLayout& image_layout, std::uint32_t side)
    : device_(device), objects_(std::make_unique<Objects>()) {
    wgpu::TextureDescriptor descriptor;
    descriptor.label = "glyph atlas";
    descriptor.dimension = wgpu::TextureDimension::e2D;
    descriptor.size = {side, side, 1};
    descriptor.format = wgpu::TextureFormat::RGBA8Unorm;
    descriptor.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
    objects_->texture = device.handle().CreateTexture(&descriptor);

    // Linear filtering is what makes distance fields scale smoothly.
    objects_->group = create_image_group(device.handle(), image_layout, objects_->texture.CreateView(),
                                         create_linear_sampler(device.handle()));
}

AtlasTexture::~AtlasTexture() = default;

const wgpu::BindGroup& AtlasTexture::group() const noexcept {
    return objects_->group;
}

void AtlasTexture::upload(std::uint32_t x, std::uint32_t y, std::uint32_t width, std::uint32_t height,
                          std::span<const std::uint8_t> rgba) {
    wgpu::TexelCopyTextureInfo destination;
    destination.texture = objects_->texture;
    destination.origin = {x, y, 0};
    wgpu::TexelCopyBufferLayout layout;
    layout.bytesPerRow = width * 4;
    layout.rowsPerImage = height;
    const wgpu::Extent3D size{width, height, 1};
    device_.queue().WriteTexture(&destination, rgba.data(), rgba.size(), &layout, &size);
}

} // namespace elg::detail::gpu
