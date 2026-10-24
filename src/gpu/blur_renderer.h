// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_GPU_BLUR_RENDERER_H
#define ELG_GPU_BLUR_RENDERER_H

#include "gpu/device.h"
#include "gpu/webgpu.h"

#include <cstdint>

namespace elg::detail::gpu {

// One direction of a separable Gaussian blur, and a box downsample ahead of wide blurs
// (shaders/blur.wgsl). Single-channel blurs (R8) make shadows; color blurs (sRGB RGBA, like
// layers) make blurred layers and backdrops.
class BlurRenderer {
public:
    static constexpr wgpu::TextureFormat kFormat = wgpu::TextureFormat::R8Unorm;
    static constexpr wgpu::TextureFormat kColorFormat = wgpu::TextureFormat::RGBA8UnormSrgb;
    // What a pass reads from its source: a single channel into kFormat, or color into
    // kColorFormat.
    enum class Channel { alpha = 0, red = 1, color = 2, inverse_alpha = 3 };

    BlurRenderer(const Device& device, const wgpu::BindGroupLayout& image_layout);

    BlurRenderer(const BlurRenderer&) = delete;
    BlurRenderer& operator=(const BlurRenderer&) = delete;

    // Records into `pass`, covering `width` x `height` at its top-left; the source's valid region
    // is the same size. `clamp_edges`: outside it repeats its edge instead of being transparent.
    void blur(const wgpu::RenderPassEncoder& pass, const wgpu::BindGroup& source, std::uint32_t width,
              std::uint32_t height, bool horizontal, float sigma, Channel channel, bool clamp_edges = false);
    // Color: each target texel averages `factor` x `factor` source texels of the `width` x `height`
    // source region (the target covers it rounded up).
    void downsample(const wgpu::RenderPassEncoder& pass, const wgpu::BindGroup& source, std::uint32_t width,
                    std::uint32_t height, std::uint32_t factor, bool clamp_edges);

private:
    wgpu::RenderPipeline single_;
    wgpu::RenderPipeline color_;
    wgpu::RenderPipeline downsample_;
};

} // namespace elg::detail::gpu

#endif // ELG_GPU_BLUR_RENDERER_H
