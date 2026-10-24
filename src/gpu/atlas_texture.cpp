// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include "gpu/atlas_texture.h"

namespace elg::detail::gpu {

AtlasTexture::AtlasTexture(const Device& device, const wgpu::BindGroupLayout& image_layout) : device_(device) {
    wgpu::TextureDescriptor descriptor;
    descriptor.label = "glyph atlas";
    descriptor.dimension = wgpu::TextureDimension::e2D;
    descriptor.size = {kAtlasSize, kAtlasSize, 1};
    descriptor.format = wgpu::TextureFormat::RGBA8Unorm;
    descriptor.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
    texture_ = device.handle().CreateTexture(&descriptor);

    // Linear filtering is what makes distance fields scale smoothly.
    group_ = create_image_group(device.handle(), image_layout, texture_.CreateView(),
                                create_linear_sampler(device.handle()));
}

void AtlasTexture::upload(std::span<const GlyphUpload> uploads) {
    for (const GlyphUpload& upload : uploads) {
        wgpu::TexelCopyTextureInfo destination;
        destination.texture = texture_;
        destination.origin = {upload.x, upload.y, 0};
        wgpu::TexelCopyBufferLayout layout;
        layout.bytesPerRow = upload.width * 4;
        layout.rowsPerImage = upload.height;
        const wgpu::Extent3D size{upload.width, upload.height, 1};
        device_.queue().WriteTexture(&destination, upload.rgba.data(), upload.rgba.size(), &layout, &size);
    }
}

} // namespace elg::detail::gpu
