// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_GPU_QUAD_RENDERER_H
#define ELG_GPU_QUAD_RENDERER_H

#include "gpu/device.h"
#include "gpu/webgpu.h"

#include <array>
#include <cstdint>
#include <utility>
#include <vector>

namespace elg::detail::gpu {

// Primitive kinds; see shaders/ui.wgsl.
enum class QuadKind : int {
    rect = 0,
    glyph = 1,
    layer = 2,
    layer_shadow = 3,
    box_shadow = 4,
    path = 5,
    image = 6,
    inner_shadow = 7,
    layer_inner_shadow = 8,
};
enum class PaintKind : int { solid = 0, linear = 1, radial = 2, elliptical = 3, angular = 4, backdrop = 5 };

// Per-instance data for shaders/ui.wgsl. Pixels of the render target, y down; colors linear and
// premultiplied.
struct QuadInstance {
    std::array<float, 4> rect{};         // x, y, width, height
    std::array<float, 4> shape{};        // radii (tl, tr, br, bl) | atlas uv | layer texel rect
    std::array<float, 4> color{};
    std::array<float, 4> border_color{};
    std::array<float, 4> params{};       // x: border width | distance range; y: kind; z: paint; w: first stop
    std::array<float, 4> gradient{};     // linear: start, end | radial: center, r0, r1
    // Clipping: the innermost rounded clip (its own rect and radii), and the intersection of every
    // enclosing clip's rect. Coverage is the product of the two.
    std::array<float, 4> clip_rect{};
    std::array<float, 4> clip_radii{};
    std::array<float, 4> extra{};        // x: stop count; y: shadow sigma
    std::array<float, 4> clip_scissor{}; // x, y, width, height
};
static_assert(sizeof(QuadInstance) == 160);

// The UI pipeline (design.md §5): rounded rects, glyphs, shadows, and layer composites as
// instanced quads in painter's order. Group 0 is an image (the glyph atlas, or a layer for
// composites); group 1 holds the frame's gradient stops and color matrices; group 2 is a second
// image (a backdrop snapshot, or a layer's content beside its inner shadow).
class QuadRenderer {
public:
    QuadRenderer(const Device& device, const wgpu::BindGroupLayout& image_layout);

    QuadRenderer(const QuadRenderer&) = delete;
    QuadRenderer& operator=(const QuadRenderer&) = delete;

    // The gradient stops' group: one read-only storage buffer.
    wgpu::BindGroup stops_group(const wgpu::Buffer& stops) const;

    // Binds everything for drawing into `pass`, whose target is `format`; draws cover `width` x
    // `height` pixels at its top-left (the target may be larger).
    void begin(const wgpu::RenderPassEncoder& pass, wgpu::TextureFormat format, std::uint32_t width,
               std::uint32_t height, const wgpu::Buffer& instances, const wgpu::BindGroup& image,
               const wgpu::BindGroup& stops, const wgpu::BindGroup& second_image);
    void bind_image(const wgpu::RenderPassEncoder& pass, const wgpu::BindGroup& image);
    void bind_second_image(const wgpu::RenderPassEncoder& pass, const wgpu::BindGroup& image);
    // Switches between "over" (the default) and "destination-in" blending, which keeps the
    // target only where the drawn quad's alpha covers it (masking a layer).
    void set_masking(const wgpu::RenderPassEncoder& pass, wgpu::TextureFormat format, bool masking);
    void draw(const wgpu::RenderPassEncoder& pass, std::uint32_t first_instance, std::uint32_t count);

private:
    struct Immediates {
        float viewport[2];
    };
    const wgpu::RenderPipeline& pipeline_for(wgpu::TextureFormat format, bool masking = false);

    const Device& device_;
    wgpu::ShaderModule module_;
    wgpu::BindGroupLayout stops_layout_;
    wgpu::PipelineLayout layout_;
    Immediates immediates_{};
    // Pipelines are specific to the target format (swapchain, layers) and blending.
    struct CachedPipeline {
        wgpu::TextureFormat format;
        bool masking;
        wgpu::RenderPipeline pipeline;
    };
    std::vector<CachedPipeline> pipelines_;
};

} // namespace elg::detail::gpu

#endif // ELG_GPU_QUAD_RENDERER_H
