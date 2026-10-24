// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include "gpu/quad_renderer.h"

#include <cstddef>

namespace elg::detail::gpu {

namespace {

// WGSL embedded at build time by elg_add_shaders() (cmake/ElgShaders.cmake).
constexpr char kUiWgsl[] =
#include "shaders/ui.wgsl.inc"
    ;

} // namespace

QuadRenderer::QuadRenderer(const Device& device, const wgpu::BindGroupLayout& image_layout) : device_(device) {
    const wgpu::Device& dev = device_.handle();
    module_ = create_shader(dev, kUiWgsl, "ui.wgsl");

    wgpu::BindGroupLayoutEntry stops{};
    stops.binding = 0;
    stops.visibility = wgpu::ShaderStage::Fragment;
    stops.buffer.type = wgpu::BufferBindingType::ReadOnlyStorage;
    wgpu::BindGroupLayoutDescriptor stops_info;
    stops_info.label = "gradient stops";
    stops_info.entryCount = 1;
    stops_info.entries = &stops;
    stops_layout_ = dev.CreateBindGroupLayout(&stops_info);

    const wgpu::BindGroupLayout layouts[] = {image_layout, stops_layout_, image_layout};
    wgpu::PipelineLayoutDescriptor layout_info;
    layout_info.label = "ui";
    layout_info.bindGroupLayoutCount = std::size(layouts);
    layout_info.bindGroupLayouts = layouts;
    layout_info.immediateSize = sizeof(Immediates);
    layout_ = dev.CreatePipelineLayout(&layout_info);
}

wgpu::BindGroup QuadRenderer::stops_group(const wgpu::Buffer& stops) const {
    wgpu::BindGroupEntry entry;
    entry.binding = 0;
    entry.buffer = stops;
    wgpu::BindGroupDescriptor descriptor;
    descriptor.layout = stops_layout_;
    descriptor.entryCount = 1;
    descriptor.entries = &entry;
    return device_.handle().CreateBindGroup(&descriptor);
}

void QuadRenderer::begin(const wgpu::RenderPassEncoder& pass, wgpu::TextureFormat format, std::uint32_t width,
                         std::uint32_t height, const wgpu::Buffer& instances, const wgpu::BindGroup& image,
                         const wgpu::BindGroup& stops, const wgpu::BindGroup& second_image) {
    pass.SetPipeline(pipeline_for(format));
    pass.SetBindGroup(0, image);
    pass.SetBindGroup(1, stops);
    pass.SetBindGroup(2, second_image);
    immediates_ = {{static_cast<float>(width), static_cast<float>(height)}};
    pass.SetImmediates(0, &immediates_, sizeof(immediates_));
    pass.SetViewport(0, 0, immediates_.viewport[0], immediates_.viewport[1], 0, 1);
    pass.SetScissorRect(0, 0, width, height);
    pass.SetVertexBuffer(0, instances);
}

void QuadRenderer::set_masking(const wgpu::RenderPassEncoder& pass, wgpu::TextureFormat format, bool masking) {
    pass.SetPipeline(pipeline_for(format, masking));
    pass.SetImmediates(0, &immediates_, sizeof(immediates_)); // not kept across pipelines
}

void QuadRenderer::bind_image(const wgpu::RenderPassEncoder& pass, const wgpu::BindGroup& image) {
    pass.SetBindGroup(0, image);
}

void QuadRenderer::bind_second_image(const wgpu::RenderPassEncoder& pass, const wgpu::BindGroup& image) {
    pass.SetBindGroup(2, image);
}

void QuadRenderer::draw(const wgpu::RenderPassEncoder& pass, std::uint32_t first_instance, std::uint32_t count) {
    if (count > 0) {
        pass.Draw(4, count, 0, first_instance);
    }
}

const wgpu::RenderPipeline& QuadRenderer::pipeline_for(wgpu::TextureFormat format, bool masking) {
    for (const CachedPipeline& cached : pipelines_) {
        if (cached.format == format && cached.masking == masking) {
            return cached.pipeline;
        }
    }

    wgpu::VertexAttribute attributes[10];
    const std::size_t offsets[] = {
        offsetof(QuadInstance, rect),      offsetof(QuadInstance, shape),        offsetof(QuadInstance, color),
        offsetof(QuadInstance, border_color), offsetof(QuadInstance, params),    offsetof(QuadInstance, gradient),
        offsetof(QuadInstance, clip_rect), offsetof(QuadInstance, clip_radii),   offsetof(QuadInstance, extra),
        offsetof(QuadInstance, clip_scissor),
    };
    for (std::uint32_t i = 0; i < std::size(attributes); ++i) {
        attributes[i].format = wgpu::VertexFormat::Float32x4;
        attributes[i].offset = offsets[i];
        attributes[i].shaderLocation = i;
    }
    wgpu::VertexBufferLayout instances;
    instances.stepMode = wgpu::VertexStepMode::Instance;
    instances.arrayStride = sizeof(QuadInstance);
    instances.attributeCount = std::size(attributes);
    instances.attributes = attributes;

    // Premultiplied alpha "over", or destination-in (dst * src.a) for masking.
    wgpu::BlendState blend;
    if (masking) {
        blend.color = {wgpu::BlendOperation::Add, wgpu::BlendFactor::Zero, wgpu::BlendFactor::SrcAlpha};
        blend.alpha = {wgpu::BlendOperation::Add, wgpu::BlendFactor::Zero, wgpu::BlendFactor::SrcAlpha};
    } else {
        blend.color = {wgpu::BlendOperation::Add, wgpu::BlendFactor::One, wgpu::BlendFactor::OneMinusSrcAlpha};
        blend.alpha = {wgpu::BlendOperation::Add, wgpu::BlendFactor::One, wgpu::BlendFactor::OneMinusSrcAlpha};
    }
    wgpu::ColorTargetState target;
    target.format = format;
    target.blend = &blend;
    wgpu::FragmentState fragment;
    fragment.module = module_;
    fragment.entryPoint = "fs_main";
    fragment.targetCount = 1;
    fragment.targets = &target;

    wgpu::RenderPipelineDescriptor descriptor;
    descriptor.label = masking ? "ui (masking)" : "ui";
    descriptor.layout = layout_;
    descriptor.vertex.module = module_;
    descriptor.vertex.entryPoint = "vs_main";
    descriptor.vertex.bufferCount = 1;
    descriptor.vertex.buffers = &instances;
    descriptor.primitive.topology = wgpu::PrimitiveTopology::TriangleStrip;
    descriptor.primitive.cullMode = wgpu::CullMode::None;
    descriptor.fragment = &fragment;
    return pipelines_.emplace_back(format, masking, device_.handle().CreateRenderPipeline(&descriptor)).pipeline;
}

} // namespace elg::detail::gpu
