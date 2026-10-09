// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_GPU_COMPOSITION_WIN32_H
#define ELG_GPU_COMPOSITION_WIN32_H

#if defined(_WIN32)

#include "gpu/device.h"
#include "gpu/vk.h"

#include <array>
#include <memory>
#include <optional>

namespace elg::detail::gpu {

// How windows present on Windows (design.md §4.1): through DirectComposition. We render into a
// D3D11 texture shared with Vulkan, then copy it into a composition swapchain.
// - Transparent windows: the swapchain has premultiplied alpha, which DWM blends with the
//   desktop (Vulkan swapchains on Windows are opaque only).
// - Every window: a composition swapchain is shown at its own pixel size, never stretched. While
//   a window is resized faster than frames arrive, the newest frame stays put at the top-left and
//   a background layer in the window's color fills the rest; a Vulkan swapchain's image would be
//   stretched to the new size instead.
class CompositionTarget {
public:
    // Throws elg::GpuError if the interop isn't available (the caller falls back to a swapchain).
    CompositionTarget(const Device& device, void* hwnd, bool transparent);
    ~CompositionTarget();

    CompositionTarget(const CompositionTarget&) = delete;
    CompositionTarget& operator=(const CompositionTarget&) = delete;

    // (Re)creates the images for `extent`. False when there's no area (minimized).
    bool prepare(VkExtent2D extent);

    // Render into this (sRGB view, premultiplied alpha), then release it to the external queue
    // family in GENERAL layout.
    VkImage image() const noexcept;
    VkImageView view() const noexcept;
    VkFormat format() const noexcept { return VK_FORMAT_B8G8R8A8_SRGB; }
    VkExtent2D extent() const noexcept { return extent_; }

    // Waits (up to `timeout_ms`) until the compositor is ready for another frame: paces frames
    // to the display without queueing them.
    void wait_until_ready(unsigned timeout_ms);
    // The color shown where no frame covers the window yet (opaque windows; sRGB).
    void set_background(const std::array<float, 4>& srgb);
    // Call once the rendering into image() has finished (its fence waited): shows it.
    void present();

    // Whether the device can do the interop at all.
    static bool supported(const Device& device);

private:
    struct Impl;
    void destroy_image() noexcept;

    const Device& device_;
    std::unique_ptr<Impl> impl_;
    VkExtent2D extent_{};
    bool transparent_;
    std::optional<std::array<float, 4>> background_;
};

} // namespace elg::detail::gpu

#endif // _WIN32

#endif // ELG_GPU_COMPOSITION_WIN32_H
