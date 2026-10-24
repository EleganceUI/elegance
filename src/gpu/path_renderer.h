// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_GPU_PATH_RENDERER_H
#define ELG_GPU_PATH_RENDERER_H

#include "gpu/device.h"
#include "gpu/render_image.h"
#include "gpu/webgpu.h"

#include <cstdint>

namespace elg::detail::gpu {

// Path coverage masks (design.md §5, "stencil-then-cover"). A path's polygons go to the GPU as
// fan triangles. Drawing them into a multisampled stencil buffer counts windings per sample
// (+1/-1 by triangle facing for non-zero, flips for even-odd); then a cover triangle writes full
// coverage wherever the stencil says "inside", and a resolve pass averages the samples into an
// anti-aliased R8 mask. The UI pass then fills the path's color or gradient through that mask.
class PathRenderer {
public:
    static constexpr wgpu::TextureFormat kMaskFormat = wgpu::TextureFormat::R8Unorm;
    static constexpr wgpu::TextureFormat kStencilFormat = wgpu::TextureFormat::Stencil8;
    static constexpr std::uint32_t kSamples = 4; // the only multisample count WebGPU guarantees

    explicit PathRenderer(const Device& device);

    PathRenderer(const PathRenderer&) = delete;
    PathRenderer& operator=(const PathRenderer&) = delete;

    // Records one mask of `width` x `height` pixels: `vertex_count` fan-triangle vertices (vec2,
    // mask pixels) from `vertices` at `first_vertex`. `samples_color` (sampled) and `stencil` are
    // multisampled scratch images at least that large; `mask` receives the coverage.
    void render(const wgpu::CommandEncoder& encoder, const RenderImage& samples_color, const RenderImage& stencil,
                const RenderImage& mask, std::uint32_t width, std::uint32_t height, const wgpu::Buffer& vertices,
                std::uint32_t first_vertex, std::uint32_t vertex_count, bool even_odd);

private:
    wgpu::RenderPipeline create_pipeline(bool cover, bool even_odd);

    const Device& device_;
    wgpu::ShaderModule module_;
    wgpu::PipelineLayout layout_;
    wgpu::RenderPipeline stencil_non_zero_;
    wgpu::RenderPipeline stencil_even_odd_;
    wgpu::RenderPipeline cover_non_zero_;
    wgpu::RenderPipeline cover_even_odd_;
    wgpu::BindGroupLayout resolve_layout_;
    wgpu::RenderPipeline resolve_;
};

} // namespace elg::detail::gpu

#endif // ELG_GPU_PATH_RENDERER_H
