// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_GPU_BLUR_RENDERER_H
#define ELG_GPU_BLUR_RENDERER_H

#include "gpu/webgpu_types.h"

#include <cstdint>
#include <memory>

namespace elg::detail::gpu {

class Device;

// One direction of a separable Gaussian blur, and a box downsample ahead of wide blurs
// (shaders/blur.wgsl). Single-channel blurs make shadows; color blurs (linear, premultiplied)
// make blurred layers and backdrops. Both in 16-bit floats: 8 bits of alpha would put steps of
// up to 13/255 into a near-opaque blur's sRGB pixels over a light background, and the result
// must stay within 4/255 of a Gaussian blur (ELGSPEC-0024.R2).
class BlurRenderer {
public:
    // R16Float and RGBA16Float (defined with WebGPU's values in the source file).
    static const wgpu::TextureFormat kFormat;
    static const wgpu::TextureFormat kColorFormat;
    // What a pass reads from its source: a single channel into kFormat, or color into
    // kColorFormat.
    enum class Channel { alpha = 0, red = 1, color = 2, inverse_alpha = 3 };

    BlurRenderer(const Device& device, const wgpu::BindGroupLayout& image_layout);
    ~BlurRenderer();

    BlurRenderer(const BlurRenderer&) = delete;
    BlurRenderer& operator=(const BlurRenderer&) = delete;
    BlurRenderer(BlurRenderer&&) = delete;
    BlurRenderer& operator=(BlurRenderer&&) = delete;

    // Records into `pass`, covering `width` x `height` at its top-left; the source's valid region
    // is the same size. `clamp_edges`: outside it repeats its edge instead of being transparent.
    void blur(const wgpu::RenderPassEncoder& pass, const wgpu::BindGroup& source, std::uint32_t width,
              std::uint32_t height, bool horizontal, float sigma, Channel channel, bool clamp_edges = false);
    // Each target texel averages `factor` x `factor` source texels of the `width` x `height` source
    // region (the target covers it rounded up): color into a color target, or what `channel` takes
    // of each texel (alpha, red, 1 - alpha) into a single-channel one.
    void downsample(const wgpu::RenderPassEncoder& pass, const wgpu::BindGroup& source, std::uint32_t width,
                    std::uint32_t height, std::uint32_t factor, bool clamp_edges, Channel channel = Channel::color);

private:
    struct Pipelines; // single-channel and color: blurs and downsamples
    std::unique_ptr<Pipelines> pipelines_;
};

} // namespace elg::detail::gpu

#endif // ELG_GPU_BLUR_RENDERER_H
