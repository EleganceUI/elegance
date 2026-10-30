// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_PLATFORM_WINDOWS_COMPOSITION_H
#define ELG_PLATFORM_WINDOWS_COMPOSITION_H

#if defined(_WIN32)

#include "gpu/device.h"
#include "gpu/webgpu.h"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>

namespace elg::detail::gpu {

// How windows present on Windows (design.md §4.1): through DirectComposition. We render into a
// D3D11 texture shared with WebGPU, then copy it into a composition swapchain.
// - Transparent windows: the swapchain has premultiplied alpha, which DWM blends with the
//   desktop (WebGPU surfaces on Windows are opaque only).
// - Every window: a composition swapchain is shown at its own pixel size, never stretched. While
//   a window is resized faster than frames arrive, the newest frame stays put at the top-left and
//   a background layer in the window's color fills the rest; a surface's image would be
//   stretched to the new size instead.
class CompositionTarget {
public:
    // Throws elg::GpuError if the interop isn't available (the caller falls back to a swapchain).
    CompositionTarget(const Device& device, void* hwnd, bool transparent);
    ~CompositionTarget();

    CompositionTarget(const CompositionTarget&) = delete;
    CompositionTarget& operator=(const CompositionTarget&) = delete;

    // (Re)creates the images for `width` x `height`. False when there's no area (minimized).
    bool prepare(std::uint32_t width, std::uint32_t height);

    // Begins WebGPU's access to the shared image and returns the view to render into (sRGB,
    // premultiplied alpha). end_access() once the frame's rendering is submitted.
    wgpu::TextureView begin_access();
    void end_access() noexcept;
    wgpu::TextureFormat view_format() const noexcept { return wgpu::TextureFormat::BGRA8UnormSrgb; }
    std::uint32_t width() const noexcept { return width_; }
    std::uint32_t height() const noexcept { return height_; }

    // Waits (up to `timeout_ms`) until the compositor is ready for another frame: paces frames
    // to the display without queueing them.
    void wait_until_ready(unsigned timeout_ms);
    // The color shown where no frame covers the window yet (opaque windows; sRGB).
    void set_background(const std::array<float, 4>& srgb);
    // Call once the rendering into the image has finished (Device::wait_idle()): shows it.
    void present();

    // Whether the device can do the interop at all.
    static bool supported(const Device& device);

private:
    struct Impl;
    void destroy_image() noexcept;

    const Device& device_;
    std::unique_ptr<Impl> impl_;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    bool transparent_;
    std::optional<std::array<float, 4>> background_;
};

// Whether windows may present through DirectComposition in this process. Not when
// ELG_PRESENT=swapchain asks for WebGPU surfaces, nor under RenderDoc: it supports neither
// DirectComposition nor sharing images with Direct3D, and the system's composition code crashes
// on its wrapped D3D11 device. Decided on the first call, before any Direct3D object exists, and
// logged.
bool composition_allowed();

} // namespace elg::detail::gpu

#endif // _WIN32

#endif // ELG_PLATFORM_WINDOWS_COMPOSITION_H
