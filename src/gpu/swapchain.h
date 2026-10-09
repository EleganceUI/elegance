// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_GPU_SWAPCHAIN_H
#define ELG_GPU_SWAPCHAIN_H

#include "gpu/device.h"
#include "gpu/vk.h"

#include <cstdint>
#include <vector>

namespace elg::detail::gpu {

// A window's swapchain: sRGB color images presented with FIFO (vsync).
class Swapchain {
public:
    // `transparent`: the window shows through where the image's alpha is below 1 (needs a
    // compositor that blends; otherwise it stays opaque).
    Swapchain(const Device& device, VkSurfaceKHR surface, bool transparent = false);
    ~Swapchain();

    Swapchain(const Swapchain&) = delete;
    Swapchain& operator=(const Swapchain&) = delete;

    // (Re)creates the swapchain if it's stale or the surface size changed. `fallback` is used
    // when the surface doesn't dictate its size. Returns false when the surface has no area
    // (e.g. a minimized window); there is nothing to present then.
    bool prepare(VkExtent2D fallback);

    // Forces recreation on the next prepare(), e.g. after VK_ERROR_OUT_OF_DATE_KHR.
    void mark_stale() noexcept { stale_ = true; }

    VkSwapchainKHR handle() const noexcept { return swapchain_; }
    VkFormat format() const noexcept { return format_.format; }
    VkExtent2D extent() const noexcept { return extent_; }
    VkImage image(std::uint32_t index) const { return images_[index]; }
    VkImageView view(std::uint32_t index) const { return views_[index]; }
    // Signaled when rendering to the image finishes; one per image, since an image's previous
    // present must be done with it before it's acquired again.
    VkSemaphore render_finished(std::uint32_t index) const { return render_finished_[index]; }

private:
    void create(VkExtent2D extent, const VkSurfaceCapabilitiesKHR& caps);
    void destroy_images() noexcept;

    const Device& device_;
    VkSurfaceKHR surface_;
    bool transparent_;
    VkSurfaceFormatKHR format_{};
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkExtent2D extent_{};
    bool stale_ = true;
    std::vector<VkImage> images_;
    std::vector<VkImageView> views_;
    std::vector<VkSemaphore> render_finished_;
};

} // namespace elg::detail::gpu

#endif // ELG_GPU_SWAPCHAIN_H
