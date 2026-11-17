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

AtlasTexture::AtlasTexture(const Device& device, const wgpu::BindGroupLayout& image_layout)
    : device_(device), objects_(std::make_unique<Objects>()) {
    wgpu::TextureDescriptor descriptor;
    descriptor.label = "glyph atlas";
    descriptor.dimension = wgpu::TextureDimension::e2D;
    descriptor.size = {kAtlasSize, kAtlasSize, 1};
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

void AtlasTexture::upload(std::span<const GlyphUpload> uploads) {
    for (const GlyphUpload& upload : uploads) {
        wgpu::TexelCopyTextureInfo destination;
        destination.texture = objects_->texture;
        destination.origin = {upload.x, upload.y, 0};
        wgpu::TexelCopyBufferLayout layout;
        layout.bytesPerRow = upload.width * 4;
        layout.rowsPerImage = upload.height;
        const wgpu::Extent3D size{upload.width, upload.height, 1};
        device_.queue().WriteTexture(&destination, upload.rgba.data(), upload.rgba.size(), &layout, &size);
    }
}

} // namespace elg::detail::gpu
