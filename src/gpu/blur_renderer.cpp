// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include "gpu/blur_renderer.h"

namespace elg::detail::gpu {

namespace {

constexpr char kBlurWgsl[] =
#include "shaders/blur.wgsl.inc"
    ;

struct Immediates {
    float region[2];
    float direction[2];
    float sigma;
    float mode;
    float clamp_edges;
    float factor;
};

} // namespace

BlurRenderer::BlurRenderer(const Device& device, const wgpu::BindGroupLayout& image_layout) {
    const wgpu::Device& dev = device.handle();
    const wgpu::ShaderModule module = create_shader(dev, kBlurWgsl, "blur.wgsl");

    wgpu::PipelineLayoutDescriptor layout_info;
    layout_info.label = "blur";
    layout_info.bindGroupLayoutCount = 1;
    layout_info.bindGroupLayouts = &image_layout;
    layout_info.immediateSize = sizeof(Immediates);
    const wgpu::PipelineLayout layout = dev.CreatePipelineLayout(&layout_info);

    const auto pipeline = [&](wgpu::TextureFormat format, const char* entry, const char* label) {
        wgpu::ColorTargetState target;
        target.format = format;
        target.writeMask = format == kFormat ? wgpu::ColorWriteMask::Red : wgpu::ColorWriteMask::All;
        wgpu::FragmentState fragment;
        fragment.module = module;
        fragment.entryPoint = entry;
        fragment.targetCount = 1;
        fragment.targets = &target;
        wgpu::RenderPipelineDescriptor descriptor;
        descriptor.label = label;
        descriptor.layout = layout;
        descriptor.vertex.module = module;
        descriptor.vertex.entryPoint = "vs_main";
        descriptor.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
        descriptor.fragment = &fragment;
        return dev.CreateRenderPipeline(&descriptor);
    };
    single_ = pipeline(kFormat, "fs_main", "blur");
    color_ = pipeline(kColorFormat, "fs_main", "blur (color)");
    downsample_ = pipeline(kColorFormat, "fs_downsample", "downsample");
}

void BlurRenderer::blur(const wgpu::RenderPassEncoder& pass, const wgpu::BindGroup& source, std::uint32_t width,
                        std::uint32_t height, bool horizontal, float sigma, Channel channel, bool clamp_edges) {
    pass.SetPipeline(channel == Channel::color ? color_ : single_);
    pass.SetBindGroup(0, source);
    const Immediates immediates{{static_cast<float>(width), static_cast<float>(height)},
                                {horizontal ? 1.f : 0.f, horizontal ? 0.f : 1.f},
                                sigma,
                                static_cast<float>(channel),
                                clamp_edges ? 1.f : 0.f,
                                1};
    pass.SetImmediates(0, &immediates, sizeof(immediates));
    pass.SetViewport(0, 0, static_cast<float>(width), static_cast<float>(height), 0, 1);
    pass.SetScissorRect(0, 0, width, height);
    pass.Draw(3);
}

void BlurRenderer::downsample(const wgpu::RenderPassEncoder& pass, const wgpu::BindGroup& source,
                              std::uint32_t width, std::uint32_t height, std::uint32_t factor, bool clamp_edges) {
    const std::uint32_t target_width = (width + factor - 1) / factor;
    const std::uint32_t target_height = (height + factor - 1) / factor;
    pass.SetPipeline(downsample_);
    pass.SetBindGroup(0, source);
    const Immediates immediates{{static_cast<float>(width), static_cast<float>(height)},
                                {0, 0},
                                0,
                                static_cast<float>(Channel::color),
                                clamp_edges ? 1.f : 0.f,
                                static_cast<float>(factor)};
    pass.SetImmediates(0, &immediates, sizeof(immediates));
    pass.SetViewport(0, 0, static_cast<float>(target_width), static_cast<float>(target_height), 0, 1);
    pass.SetScissorRect(0, 0, target_width, target_height);
    pass.Draw(3);
}

} // namespace elg::detail::gpu
