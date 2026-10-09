// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include "gpu/render_image.h"

#include <utility>

namespace elg::detail::gpu {

RenderImage::RenderImage(const Device& device, VkFormat format, std::uint32_t width, std::uint32_t height,
                         VkSampleCountFlagBits samples, VkImageUsageFlags usage, VkImageAspectFlags aspect)
    : device_(&device), format_(format), width_(width), height_(height) {
    VkImageCreateInfo image_info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.format = format;
    image_info.extent = {width, height, 1};
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.samples = samples;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.usage = usage;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VmaAllocationCreateInfo allocation_info{};
    allocation_info.usage = VMA_MEMORY_USAGE_AUTO;
    allocation_info.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
    check(vmaCreateImage(device.allocator(), &image_info, &allocation_info, &image_, &allocation_, nullptr),
          "create an offscreen image");

    VkImageViewCreateInfo view_info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view_info.image = image_;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = format;
    view_info.subresourceRange = {aspect, 0, 1, 0, 1};
    const VkResult result = vkCreateImageView(device.handle(), &view_info, nullptr, &view_);
    if (result != VK_SUCCESS) {
        release();
        throw_vk_error(result, "create an offscreen image view");
    }
}

RenderImage::~RenderImage() {
    release();
}

RenderImage::RenderImage(RenderImage&& other) noexcept
    : device_(other.device_), format_(other.format_), width_(other.width_), height_(other.height_),
      image_(std::exchange(other.image_, VK_NULL_HANDLE)),
      allocation_(std::exchange(other.allocation_, VK_NULL_HANDLE)),
      view_(std::exchange(other.view_, VK_NULL_HANDLE)) {}

RenderImage& RenderImage::operator=(RenderImage&& other) noexcept {
    if (this != &other) {
        release();
        device_ = other.device_;
        format_ = other.format_;
        width_ = other.width_;
        height_ = other.height_;
        image_ = std::exchange(other.image_, VK_NULL_HANDLE);
        allocation_ = std::exchange(other.allocation_, VK_NULL_HANDLE);
        view_ = std::exchange(other.view_, VK_NULL_HANDLE);
    }
    return *this;
}

void RenderImage::release() noexcept {
    if (view_) {
        vkDestroyImageView(device_->handle(), view_, nullptr);
        view_ = VK_NULL_HANDLE;
    }
    if (image_) {
        vmaDestroyImage(device_->allocator(), image_, allocation_);
        image_ = VK_NULL_HANDLE;
        allocation_ = VK_NULL_HANDLE;
    }
}

} // namespace elg::detail::gpu
