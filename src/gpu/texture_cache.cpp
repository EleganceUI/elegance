// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include "gpu/texture_cache.h"

#include "core/log.h"

#include <algorithm>
#include <bit>
#include <utility>

namespace elg::detail::gpu {

namespace {

constexpr char kMipmapWgsl[] =
#include "shaders/mipmap.wgsl.inc"
    ;

constexpr wgpu::TextureFormat kFormat = wgpu::TextureFormat::RGBA8UnormSrgb; // sampling yields linear color

} // namespace

TextureCache::TextureCache(const Device& device, wgpu::BindGroupLayout image_layout, CachePolicy policy)
    : device_(device), layout_(std::move(image_layout)), sampler_(create_linear_sampler(device.handle())),
      policy_(policy) {
    const wgpu::Device& dev = device.handle();
    const wgpu::ShaderModule module = create_shader(dev, kMipmapWgsl, "mipmap.wgsl");
    wgpu::PipelineLayoutDescriptor layout_info;
    layout_info.label = "mipmap";
    layout_info.bindGroupLayoutCount = 1;
    layout_info.bindGroupLayouts = &layout_;
    const wgpu::PipelineLayout layout = dev.CreatePipelineLayout(&layout_info);

    wgpu::ColorTargetState target;
    target.format = kFormat;
    wgpu::FragmentState fragment;
    fragment.module = module;
    fragment.entryPoint = "fs_main";
    fragment.targetCount = 1;
    fragment.targets = &target;
    wgpu::RenderPipelineDescriptor descriptor;
    descriptor.label = "mipmap";
    descriptor.layout = layout;
    descriptor.vertex.module = module;
    descriptor.vertex.entryPoint = "vs_main";
    descriptor.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
    descriptor.fragment = &fragment;
    mip_pipeline_ = dev.CreateRenderPipeline(&descriptor);
}

wgpu::BindGroup TextureCache::acquire(const wgpu::CommandEncoder& encoder, const DecodedImage& image) {
    if (const auto it = textures_.find(image.id); it != textures_.end()) {
        frame_.mark(it->second.use);
        return it->second.group;
    }
    if (image.width > device_.max_texture_side() || image.height > device_.max_texture_side()) {
        if (!warned_) {
            warned_ = true;
            log_warning("an image of {} x {} pixels isn't drawn: the GPU's textures are at most {} pixels a side",
                        image.width, image.height, device_.max_texture_side());
        }
        return nullptr;
    }
    const auto levels = static_cast<std::uint32_t>(std::bit_width(std::max(image.width, image.height)));

    Texture texture;
    wgpu::TextureDescriptor descriptor;
    descriptor.label = "image";
    descriptor.dimension = wgpu::TextureDimension::e2D;
    descriptor.size = {image.width, image.height, 1};
    descriptor.format = kFormat;
    descriptor.mipLevelCount = levels;
    descriptor.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst |
                       wgpu::TextureUsage::RenderAttachment; // mips are rendered
    texture.texture = device_.handle().CreateTexture(&descriptor);
    texture.group = create_image_group(device_.handle(), layout_, texture.texture.CreateView(), sampler_);

    // Level 0 through the queue (ahead of this frame's commands), then each level rendered
    // from the one above.
    wgpu::TexelCopyTextureInfo destination;
    destination.texture = texture.texture;
    wgpu::TexelCopyBufferLayout layout;
    layout.bytesPerRow = image.width * 4;
    layout.rowsPerImage = image.height;
    const wgpu::Extent3D size{image.width, image.height, 1};
    device_.queue().WriteTexture(&destination, image.rgba.data(), image.rgba.size(), &layout, &size);
    generate_mips(encoder, texture.texture, levels);

    texture.use.bytes = decoded_texture_bytes(image.width, image.height);
    frame_.mark(texture.use);
    return textures_.emplace(image.id, std::move(texture)).first->second.group;
}

void TextureCache::generate_mips(const wgpu::CommandEncoder& encoder, const wgpu::Texture& texture,
                                 std::uint32_t levels) {
    for (std::uint32_t level = 1; level < levels; ++level) {
        wgpu::TextureViewDescriptor view;
        view.baseMipLevel = level - 1;
        view.mipLevelCount = 1;
        const wgpu::BindGroup source =
            create_image_group(device_.handle(), layout_, texture.CreateView(&view), sampler_);
        view.baseMipLevel = level;

        wgpu::RenderPassColorAttachment color;
        color.view = texture.CreateView(&view);
        color.loadOp = wgpu::LoadOp::Clear;
        color.storeOp = wgpu::StoreOp::Store;
        wgpu::RenderPassDescriptor pass_info;
        pass_info.label = "mipmap";
        pass_info.colorAttachmentCount = 1;
        pass_info.colorAttachments = &color;
        const wgpu::RenderPassEncoder pass = encoder.BeginRenderPass(&pass_info);
        pass.SetPipeline(mip_pipeline_);
        pass.SetBindGroup(0, source);
        pass.Draw(3);
        pass.End();
    }
}

void TextureCache::end_frame() {
    evict(textures_, frame_, policy_);
}

} // namespace elg::detail::gpu
