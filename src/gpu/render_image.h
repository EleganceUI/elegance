// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_GPU_RENDER_IMAGE_H
#define ELG_GPU_RENDER_IMAGE_H

#include "gpu/device.h"
#include "gpu/vk.h"

#include <cstdint>

namespace elg::detail::gpu {

// An offscreen image that is rendered to and then sampled (layers, blur passes), or a
// multisampled / depth-stencil scratch attachment (path masks).
class RenderImage {
public:
    RenderImage(const Device& device, VkFormat format, std::uint32_t width, std::uint32_t height,
                VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT,
                VkImageUsageFlags usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT);
    ~RenderImage();

    RenderImage(RenderImage&& other) noexcept;
    RenderImage& operator=(RenderImage&& other) noexcept;
    RenderImage(const RenderImage&) = delete;
    RenderImage& operator=(const RenderImage&) = delete;

    VkImage image() const noexcept { return image_; }
    VkImageView view() const noexcept { return view_; }
    VkFormat format() const noexcept { return format_; }
    std::uint32_t width() const noexcept { return width_; }
    std::uint32_t height() const noexcept { return height_; }

private:
    void release() noexcept;

    const Device* device_;
    VkFormat format_;
    std::uint32_t width_;
    std::uint32_t height_;
    VkImage image_ = VK_NULL_HANDLE;
    VmaAllocation allocation_ = VK_NULL_HANDLE;
    VkImageView view_ = VK_NULL_HANDLE;
};

} // namespace elg::detail::gpu

#endif // ELG_GPU_RENDER_IMAGE_H
