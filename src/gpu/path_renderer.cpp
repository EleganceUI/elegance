// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include "gpu/path_renderer.h"

namespace elg::detail::gpu {

namespace {

constexpr char kPathWgsl[] =
#include "shaders/path.wgsl.inc"
    ;

struct Immediates {
    float viewport[2];
};

} // namespace

PathRenderer::PathRenderer(const Device& device) : device_(device) {
    const wgpu::Device& dev = device_.handle();
    module_ = create_shader(dev, kPathWgsl, "path.wgsl");

    wgpu::PipelineLayoutDescriptor layout_info;
    layout_info.label = "path";
    layout_info.immediateSize = sizeof(Immediates);
    layout_ = dev.CreatePipelineLayout(&layout_info);
    stencil_non_zero_ = create_pipeline(false, false);
    stencil_even_odd_ = create_pipeline(false, true);
    cover_non_zero_ = create_pipeline(true, false);
    cover_even_odd_ = create_pipeline(true, true);

    // The resolve: reads every sample of the coverage, writes the mask.
    wgpu::BindGroupLayoutEntry samples{};
    samples.binding = 0;
    samples.visibility = wgpu::ShaderStage::Fragment;
    samples.texture.sampleType = wgpu::TextureSampleType::UnfilterableFloat;
    samples.texture.viewDimension = wgpu::TextureViewDimension::e2D;
    samples.texture.multisampled = true;
    wgpu::BindGroupLayoutDescriptor group_info;
    group_info.label = "path samples";
    group_info.entryCount = 1;
    group_info.entries = &samples;
    resolve_layout_ = dev.CreateBindGroupLayout(&group_info);
    wgpu::PipelineLayoutDescriptor resolve_layout_info;
    resolve_layout_info.label = "path resolve";
    resolve_layout_info.bindGroupLayoutCount = 1;
    resolve_layout_info.bindGroupLayouts = &resolve_layout_;
    const wgpu::PipelineLayout resolve_layout = dev.CreatePipelineLayout(&resolve_layout_info);

    wgpu::ColorTargetState target;
    target.format = kMaskFormat;
    target.writeMask = wgpu::ColorWriteMask::Red;
    wgpu::FragmentState fragment;
    fragment.module = module_;
    fragment.entryPoint = "fs_resolve";
    fragment.targetCount = 1;
    fragment.targets = &target;
    wgpu::RenderPipelineDescriptor descriptor;
    descriptor.label = "path resolve";
    descriptor.layout = resolve_layout;
    descriptor.vertex.module = module_;
    descriptor.vertex.entryPoint = "vs_cover";
    descriptor.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
    descriptor.fragment = &fragment;
    resolve_ = dev.CreateRenderPipeline(&descriptor);
}

wgpu::RenderPipeline PathRenderer::create_pipeline(bool cover, bool even_odd) {
    const wgpu::VertexAttribute position{.format = wgpu::VertexFormat::Float32x2, .offset = 0, .shaderLocation = 0};
    wgpu::VertexBufferLayout vertices;
    vertices.stepMode = wgpu::VertexStepMode::Vertex;
    vertices.arrayStride = 2 * sizeof(float);
    vertices.attributeCount = 1;
    vertices.attributes = &position;

    wgpu::DepthStencilState depth_stencil;
    depth_stencil.format = kStencilFormat;
    depth_stencil.depthWriteEnabled = false;
    depth_stencil.depthCompare = wgpu::CompareFunction::Always;
    if (!cover) {
        // Count windings: +1 for triangles one way round, -1 the other (non-zero), or flip the
        // low bit (even-odd).
        depth_stencil.stencilFront = {wgpu::CompareFunction::Always, wgpu::StencilOperation::Keep,
                                      wgpu::StencilOperation::Keep,
                                      even_odd ? wgpu::StencilOperation::Invert : wgpu::StencilOperation::IncrementWrap};
        depth_stencil.stencilBack = depth_stencil.stencilFront;
        depth_stencil.stencilBack.passOp =
            even_odd ? wgpu::StencilOperation::Invert : wgpu::StencilOperation::DecrementWrap;
        depth_stencil.stencilReadMask = 0xFF;
        depth_stencil.stencilWriteMask = 0xFF;
    } else {
        // Inside where the count is non-zero (or odd): compared with the reference, 0.
        depth_stencil.stencilFront = {wgpu::CompareFunction::NotEqual, wgpu::StencilOperation::Keep,
                                      wgpu::StencilOperation::Keep, wgpu::StencilOperation::Keep};
        depth_stencil.stencilBack = depth_stencil.stencilFront;
        depth_stencil.stencilReadMask = even_odd ? 1u : 0xFFu;
        depth_stencil.stencilWriteMask = 0;
    }

    wgpu::ColorTargetState target;
    target.format = kMaskFormat;
    target.writeMask = cover ? wgpu::ColorWriteMask::Red : wgpu::ColorWriteMask::None;
    wgpu::FragmentState fragment;
    fragment.module = module_;
    fragment.entryPoint = "fs_coverage";
    fragment.targetCount = 1;
    fragment.targets = &target;

    wgpu::RenderPipelineDescriptor descriptor;
    descriptor.label = cover ? "path cover" : "path stencil";
    descriptor.layout = layout_;
    descriptor.vertex.module = module_;
    descriptor.vertex.entryPoint = cover ? "vs_cover" : "vs_fill";
    descriptor.vertex.bufferCount = cover ? 0 : 1;
    descriptor.vertex.buffers = cover ? nullptr : &vertices;
    descriptor.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
    descriptor.primitive.cullMode = wgpu::CullMode::None; // both facings count: that's the winding
    descriptor.depthStencil = &depth_stencil;
    descriptor.multisample.count = kSamples;
    descriptor.fragment = &fragment;
    return device_.handle().CreateRenderPipeline(&descriptor);
}

void PathRenderer::render(const wgpu::CommandEncoder& encoder, const RenderImage& samples_color,
                          const RenderImage& stencil, const RenderImage& mask, std::uint32_t width,
                          std::uint32_t height, const wgpu::Buffer& vertices, std::uint32_t first_vertex,
                          std::uint32_t vertex_count, bool even_odd) {
    {
        wgpu::RenderPassColorAttachment color;
        color.view = samples_color.view();
        color.loadOp = wgpu::LoadOp::Clear;
        color.storeOp = wgpu::StoreOp::Store; // read by the resolve
        wgpu::RenderPassDepthStencilAttachment depth_stencil;
        depth_stencil.view = stencil.view();
        depth_stencil.stencilLoadOp = wgpu::LoadOp::Clear;
        depth_stencil.stencilStoreOp = wgpu::StoreOp::Discard;
        wgpu::RenderPassDescriptor descriptor;
        descriptor.label = "path coverage";
        descriptor.colorAttachmentCount = 1;
        descriptor.colorAttachments = &color;
        descriptor.depthStencilAttachment = &depth_stencil;
        const wgpu::RenderPassEncoder pass = encoder.BeginRenderPass(&descriptor);
        pass.SetViewport(0, 0, static_cast<float>(width), static_cast<float>(height), 0, 1);
        pass.SetScissorRect(0, 0, width, height);

        const Immediates immediates{{static_cast<float>(width), static_cast<float>(height)}};
        pass.SetPipeline(even_odd ? stencil_even_odd_ : stencil_non_zero_);
        pass.SetImmediates(0, &immediates, sizeof(immediates));
        pass.SetVertexBuffer(0, vertices);
        pass.Draw(vertex_count, 1, first_vertex, 0);

        pass.SetPipeline(even_odd ? cover_even_odd_ : cover_non_zero_);
        pass.Draw(3);
        pass.End();
    }

    wgpu::BindGroupEntry entry;
    entry.binding = 0;
    entry.textureView = samples_color.view();
    wgpu::BindGroupDescriptor group;
    group.layout = resolve_layout_;
    group.entryCount = 1;
    group.entries = &entry;
    const wgpu::BindGroup samples = device_.handle().CreateBindGroup(&group);

    wgpu::RenderPassColorAttachment color;
    color.view = mask.view();
    color.loadOp = wgpu::LoadOp::Clear;
    color.storeOp = wgpu::StoreOp::Store;
    wgpu::RenderPassDescriptor descriptor;
    descriptor.label = "path resolve";
    descriptor.colorAttachmentCount = 1;
    descriptor.colorAttachments = &color;
    const wgpu::RenderPassEncoder pass = encoder.BeginRenderPass(&descriptor);
    pass.SetPipeline(resolve_);
    pass.SetBindGroup(0, samples);
    pass.Draw(3);
    pass.End();
}

} // namespace elg::detail::gpu
