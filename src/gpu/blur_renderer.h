// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_GPU_BLUR_RENDERER_H
#define ELG_GPU_BLUR_RENDERER_H

#include "gpu/device.h"
#include "gpu/vk.h"

#include <cstdint>

namespace elg::detail::gpu {

// One direction of a separable Gaussian blur into a single-channel (R8) target; two passes make
// a layer's shadow (shaders/blur.frag).
class BlurRenderer {
public:
    static constexpr VkFormat kFormat = VK_FORMAT_R8_UNORM;
    enum class Channel { alpha = 0, red = 1 };

    BlurRenderer(const Device& device, VkDescriptorSetLayout image_layout);
    ~BlurRenderer();

    BlurRenderer(const BlurRenderer&) = delete;
    BlurRenderer& operator=(const BlurRenderer&) = delete;

    // Records into an active dynamic-rendering pass on an R8 target of `width` x `height`.
    void blur(VkCommandBuffer cmd, VkDescriptorSet source, std::uint32_t width, std::uint32_t height,
              bool horizontal, float sigma, Channel channel);

private:
    void destroy() noexcept;

    const Device& device_;
    VkShaderModule vertex_ = VK_NULL_HANDLE;
    VkShaderModule fragment_ = VK_NULL_HANDLE;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
};

} // namespace elg::detail::gpu

#endif // ELG_GPU_BLUR_RENDERER_H
