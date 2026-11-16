// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_GPU_PATH_RENDERER_H
#define ELG_GPU_PATH_RENDERER_H

#include "gpu/webgpu_types.h"

#include <cstdint>
#include <memory>

namespace elg::detail::gpu {

class Device;

// Path coverage masks (design.md §5, "stencil-then-cover"). A path's polygons go to the GPU as
// fan triangles. Drawing them into a multisampled stencil buffer counts windings per sample
// (+1/-1 by triangle facing for non-zero, flips for even-odd); then a cover triangle writes full
// coverage wherever the stencil says "inside", and a resolve pass averages the samples into an
// anti-aliased R8 mask. The UI pass then fills the path's color or gradient through that mask.
class PathRenderer {
public:
    // R8Unorm and Stencil8 (defined with WebGPU's values in the source file).
    static const wgpu::TextureFormat kMaskFormat;
    static const wgpu::TextureFormat kStencilFormat;
    static constexpr std::uint32_t kSamples = 4; // the only multisample count WebGPU guarantees

    explicit PathRenderer(const Device& device);
    ~PathRenderer();

    PathRenderer(const PathRenderer&) = delete;
    PathRenderer& operator=(const PathRenderer&) = delete;
    PathRenderer(PathRenderer&&) = delete;
    PathRenderer& operator=(PathRenderer&&) = delete;

    // Records one mask of `width` x `height` pixels: `vertex_count` fan-triangle vertices (vec2,
    // mask pixels) from `vertices` at `first_vertex`. `samples_color` (sampled) and `stencil` view
    // multisampled scratch images at least that large; `mask` receives the coverage.
    void render(const wgpu::CommandEncoder& encoder, const wgpu::TextureView& samples_color,
                const wgpu::TextureView& stencil, const wgpu::TextureView& mask, std::uint32_t width,
                std::uint32_t height, const wgpu::Buffer& vertices, std::uint32_t first_vertex,
                std::uint32_t vertex_count, bool even_odd);

private:
    struct Pipelines; // the shader, the stencil and cover pipelines, and the resolve
    wgpu::RenderPipeline create_pipeline(bool cover, bool even_odd);

    const Device& device_;
    std::unique_ptr<Pipelines> pipelines_;
};

} // namespace elg::detail::gpu

#endif // ELG_GPU_PATH_RENDERER_H
