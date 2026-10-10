// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_GPU_QUAD_RENDERER_H
#define ELG_GPU_QUAD_RENDERER_H

#include "gpu/device.h"
#include "gpu/vk.h"

#include <array>
#include <cstdint>
#include <utility>
#include <vector>

namespace elg::detail::gpu {

// Primitive kinds; see shaders/ui.frag.
enum class QuadKind : int { rect = 0, glyph = 1, layer = 2, layer_shadow = 3, box_shadow = 4, path = 5, image = 6 };
enum class PaintKind : int { solid = 0, linear = 1, radial = 2 };

// Per-instance data for shaders/ui.vert. Pixels of the render target, y down; colors linear and
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
// instanced quads in painter's order. Set 0 is an image (the glyph atlas, or a layer for
// composites); set 1 holds the frame's gradient stops.
class QuadRenderer {
public:
    QuadRenderer(const Device& device, VkDescriptorSetLayout image_layout);
    ~QuadRenderer();

    QuadRenderer(const QuadRenderer&) = delete;
    QuadRenderer& operator=(const QuadRenderer&) = delete;

    VkDescriptorSetLayout stops_layout() const noexcept { return stops_layout_; }

    // Binds everything for drawing into an active dynamic-rendering pass of `extent` pixels.
    void begin(VkCommandBuffer cmd, VkFormat color_format, VkExtent2D extent, VkBuffer instances,
               VkDescriptorSet image, VkDescriptorSet stops);
    void bind_image(VkCommandBuffer cmd, VkDescriptorSet image);
    // Switches between "over" (the default) and "destination-in" blending, which keeps the
    // target only where the drawn quad's alpha covers it (masking a layer).
    void set_masking(VkCommandBuffer cmd, VkFormat color_format, bool masking);
    void draw(VkCommandBuffer cmd, std::uint32_t first_instance, std::uint32_t count);

private:
    VkPipeline pipeline_for(VkFormat format, bool masking = false);
    void destroy() noexcept;

    const Device& device_;
    VkShaderModule vertex_ = VK_NULL_HANDLE;
    VkShaderModule fragment_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout stops_layout_ = VK_NULL_HANDLE;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    // Pipelines are specific to the target format (swapchain, layers) and blending.
    struct CachedPipeline {
        VkFormat format;
        bool masking;
        VkPipeline pipeline;
    };
    std::vector<CachedPipeline> pipelines_;
};

} // namespace elg::detail::gpu

#endif // ELG_GPU_QUAD_RENDERER_H
