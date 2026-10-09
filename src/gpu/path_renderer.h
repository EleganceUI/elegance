// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_GPU_PATH_RENDERER_H
#define ELG_GPU_PATH_RENDERER_H

#include "gpu/device.h"
#include "gpu/render_image.h"
#include "gpu/vk.h"

#include <cstdint>

namespace elg::detail::gpu {

// Path coverage masks (design.md §5, "stencil-then-cover"). A path's polygons go to the GPU as
// fan triangles. Drawing them into a multisampled stencil buffer counts windings per sample
// (+1/-1 by triangle facing for non-zero, flips for even-odd); then a cover triangle writes full
// coverage wherever the stencil says "inside", and the MSAA resolve averages the samples into an
// anti-aliased R8 mask. The UI pass then fills the path's color or gradient through that mask.
class PathRenderer {
public:
    static constexpr VkFormat kMaskFormat = VK_FORMAT_R8_UNORM;

    explicit PathRenderer(const Device& device);
    ~PathRenderer();

    PathRenderer(const PathRenderer&) = delete;
    PathRenderer& operator=(const PathRenderer&) = delete;

    VkFormat stencil_format() const noexcept { return stencil_format_; }
    VkImageAspectFlags stencil_aspect() const noexcept { return stencil_aspect_; }
    VkSampleCountFlagBits samples() const noexcept { return samples_; }

    // Records one mask of `extent` pixels: `vertex_count` fan-triangle vertices (vec2, mask
    // pixels) from `vertices` at `first_vertex`. `samples_color` and `stencil` are multisampled
    // scratch images; `mask` receives the coverage, left ready for sampling.
    void render(VkCommandBuffer cmd, const RenderImage& samples_color, const RenderImage& stencil, const RenderImage& mask,
                VkExtent2D extent, VkBuffer vertices, std::uint32_t first_vertex, std::uint32_t vertex_count,
                bool even_odd);

private:
    VkPipeline create_pipeline(bool cover, bool even_odd);
    void destroy() noexcept;

    const Device& device_;
    VkFormat stencil_format_ = VK_FORMAT_UNDEFINED;
    VkImageAspectFlags stencil_aspect_ = 0;
    VkSampleCountFlagBits samples_ = VK_SAMPLE_COUNT_1_BIT;
    VkShaderModule fill_vertex_ = VK_NULL_HANDLE;
    VkShaderModule cover_vertex_ = VK_NULL_HANDLE;
    VkShaderModule fragment_ = VK_NULL_HANDLE;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkPipeline stencil_non_zero_ = VK_NULL_HANDLE;
    VkPipeline stencil_even_odd_ = VK_NULL_HANDLE;
    VkPipeline cover_non_zero_ = VK_NULL_HANDLE;
    VkPipeline cover_even_odd_ = VK_NULL_HANDLE;
};

} // namespace elg::detail::gpu

#endif // ELG_GPU_PATH_RENDERER_H
