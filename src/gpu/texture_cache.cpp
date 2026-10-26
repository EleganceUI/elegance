// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include "gpu/texture_cache.h"

#include <algorithm>
#include <bit>

namespace elg::detail::gpu {

namespace {

constexpr char kMipmapWgsl[] =
#include "shaders/mipmap.wgsl.inc"
    ;

constexpr wgpu::TextureFormat kFormat = wgpu::TextureFormat::RGBA8UnormSrgb; // sampling yields linear color
// Dropped when unused for this many frames (of any window), or sooner when over the budget.
constexpr std::uint64_t kIdleFrames = 600;
// Over the budget, textures used this recently are kept anyway (they're on screen).
constexpr std::uint64_t kRecentFrames = 16;
constexpr std::size_t kBudgetBytes = 512ull << 20;

} // namespace

TextureCache::TextureCache(const Device& device, const wgpu::BindGroupLayout& image_layout)
    : device_(device), layout_(image_layout), sampler_(create_linear_sampler(device.handle())) {
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

void TextureCache::destroy(Texture& texture) noexcept {
    bytes_ -= texture.bytes;
    texture = {};
}

wgpu::BindGroup TextureCache::acquire(const wgpu::CommandEncoder& encoder, const DecodedImage& image) {
    if (const auto it = textures_.find(image.id); it != textures_.end()) {
        it->second.last_used = frame_;
        return it->second.group;
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

    texture.bytes = image.rgba.size() * 4 / 3; // with mips
    texture.last_used = frame_;
    bytes_ += texture.bytes;
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

void TextureCache::end_frame() noexcept {
    ++frame_;
    std::erase_if(textures_, [&](auto& entry) {
        Texture& texture = entry.second;
        if (frame_ - texture.last_used > kIdleFrames) {
            destroy(texture);
            return true;
        }
        return false;
    });
    // Over budget: the least recently used first, but not what recent frames drew.
    while (bytes_ > kBudgetBytes) {
        auto oldest = textures_.end();
        for (auto it = textures_.begin(); it != textures_.end(); ++it) {
            if (oldest == textures_.end() || it->second.last_used < oldest->second.last_used) {
                oldest = it;
            }
        }
        if (oldest == textures_.end() || frame_ - oldest->second.last_used <= kRecentFrames) {
            break;
        }
        destroy(oldest->second);
        textures_.erase(oldest);
    }
}

} // namespace elg::detail::gpu
