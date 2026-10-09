// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_GPU_COMPOSITION_WIN32_H
#define ELG_GPU_COMPOSITION_WIN32_H

#if defined(_WIN32)

#include "gpu/device.h"
#include "gpu/vk.h"

#include <memory>

namespace elg::detail::gpu {

// Per-pixel transparent windows on Windows (design.md §4.1). Vulkan swapchains there are opaque
// (drivers offer only VK_COMPOSITE_ALPHA_OPAQUE), so a transparent window is presented through
// DirectComposition instead: we render into a D3D11 texture shared with Vulkan, then copy it into
// a composition swapchain with premultiplied alpha that DWM blends with the desktop.
class CompositionTarget {
public:
    // Throws elg::GpuError if the interop isn't available (the caller falls back to a swapchain).
    CompositionTarget(const Device& device, void* hwnd);
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
};

} // namespace elg::detail::gpu

#endif // _WIN32

#endif // ELG_GPU_COMPOSITION_WIN32_H
