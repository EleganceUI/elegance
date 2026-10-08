// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include "gpu/swapchain.h"

#include <elg/error.h>

#include <algorithm>
#include <limits>

namespace elg::detail::gpu {

namespace {

VkSurfaceFormatKHR choose_format(VkPhysicalDevice physical, VkSurfaceKHR surface) {
    std::uint32_t count = 0;
    check(vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &count, nullptr),
          "query surface formats");
    std::vector<VkSurfaceFormatKHR> formats(count);
    check(vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &count, formats.data()),
          "query surface formats");
    if (formats.empty()) {
        throw GpuError("the window surface reports no pixel formats");
    }
    // sRGB formats let the shaders work in linear space; the hardware encodes on write.
    for (const VkSurfaceFormatKHR& format : formats) {
        if ((format.format == VK_FORMAT_B8G8R8A8_SRGB || format.format == VK_FORMAT_R8G8B8A8_SRGB) &&
            format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return format;
        }
    }
    return formats.front();
}

VkCompositeAlphaFlagBitsKHR choose_composite_alpha(VkCompositeAlphaFlagsKHR supported) {
    for (VkCompositeAlphaFlagBitsKHR mode :
         {VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
          VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR}) {
        if (supported & mode) {
            return mode;
        }
    }
    return VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
}

} // namespace

Swapchain::Swapchain(const Device& device, VkSurfaceKHR surface)
    : device_(device), surface_(surface), format_(choose_format(device.physical(), surface)) {}

Swapchain::~Swapchain() {
    if (swapchain_) {
        vkDeviceWaitIdle(device_.handle());
        destroy_images();
        vkDestroySwapchainKHR(device_.handle(), swapchain_, nullptr);
    }
}

bool Swapchain::prepare(VkExtent2D fallback) {
    VkSurfaceCapabilitiesKHR caps;
    check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device_.physical(), surface_, &caps),
          "query surface capabilities");

    VkExtent2D extent = caps.currentExtent;
    if (extent.width == std::numeric_limits<std::uint32_t>::max()) {
        extent.width = std::clamp(fallback.width, caps.minImageExtent.width, caps.maxImageExtent.width);
        extent.height = std::clamp(fallback.height, caps.minImageExtent.height, caps.maxImageExtent.height);
    }
    if (extent.width == 0 || extent.height == 0) {
        return false;
    }
    if (!stale_ && swapchain_ && extent.width == extent_.width && extent.height == extent_.height) {
        return true;
    }
    create(extent, caps);
    return true;
}

void Swapchain::create(VkExtent2D extent, const VkSurfaceCapabilitiesKHR& caps) {
    // Simple and safe: nothing may still use the old images. Resizes are rare.
    device_.wait_idle();

    std::uint32_t image_count = caps.minImageCount + 1;
    if (caps.maxImageCount != 0) {
        image_count = std::min(image_count, caps.maxImageCount);
    }

    VkSwapchainCreateInfoKHR info{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    info.surface = surface_;
    info.minImageCount = image_count;
    info.imageFormat = format_.format;
    info.imageColorSpace = format_.colorSpace;
    info.imageExtent = extent;
    info.imageArrayLayers = 1;
    info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.preTransform = caps.currentTransform;
    info.compositeAlpha = choose_composite_alpha(caps.supportedCompositeAlpha);
    info.presentMode = VK_PRESENT_MODE_FIFO_KHR; // always available; vsync, no tearing
    info.clipped = VK_TRUE;
    info.oldSwapchain = swapchain_;

    VkSwapchainKHR created = VK_NULL_HANDLE;
    check(vkCreateSwapchainKHR(device_.handle(), &info, nullptr, &created), "create the swapchain");

    destroy_images();
    if (swapchain_) {
        vkDestroySwapchainKHR(device_.handle(), swapchain_, nullptr);
    }
    swapchain_ = created;
    extent_ = extent;
    stale_ = false;

    std::uint32_t count = 0;
    check(vkGetSwapchainImagesKHR(device_.handle(), swapchain_, &count, nullptr), "get swapchain images");
    images_.resize(count);
    check(vkGetSwapchainImagesKHR(device_.handle(), swapchain_, &count, images_.data()),
          "get swapchain images");

    for (VkImage image : images_) {
        VkImageViewCreateInfo view_info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view_info.image = image;
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_info.format = format_.format;
        view_info.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VkImageView view = VK_NULL_HANDLE;
        check(vkCreateImageView(device_.handle(), &view_info, nullptr, &view), "create an image view");
        views_.push_back(view);

        VkSemaphoreCreateInfo semaphore_info{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VkSemaphore semaphore = VK_NULL_HANDLE;
        check(vkCreateSemaphore(device_.handle(), &semaphore_info, nullptr, &semaphore),
              "create a semaphore");
        render_finished_.push_back(semaphore);
    }
}

void Swapchain::destroy_images() noexcept {
    for (VkImageView view : views_) {
        vkDestroyImageView(device_.handle(), view, nullptr);
    }
    for (VkSemaphore semaphore : render_finished_) {
        vkDestroySemaphore(device_.handle(), semaphore, nullptr);
    }
    views_.clear();
    render_finished_.clear();
    images_.clear();
}

} // namespace elg::detail::gpu
