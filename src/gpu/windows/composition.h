// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_GPU_WINDOWS_COMPOSITION_H
#define ELG_GPU_WINDOWS_COMPOSITION_H

#if defined(_WIN32)

#include "gpu/device.h"
#include "gpu/presenter.h"
#include "gpu/webgpu.h"

#include <webgpu/webgpu_cpp.h>

#include <array>
#include <cstdint>
#include <memory>
#include <optional>

struct ID3D11Fence; // d3d11_3.h

namespace elg::detail::gpu {

// How windows present on Windows (design.md §4.1): through DirectComposition. We render into a
// D3D11 texture shared with WebGPU, then copy it into a composition swapchain.
// - Transparent windows: the swapchain has premultiplied alpha, which DWM blends with the
//   desktop (WebGPU surfaces on Windows are opaque only).
// - Every window: a composition swapchain is shown at its own pixel size, never stretched. While
//   a window is resized faster than frames arrive, the newest frame stays put at the top-left and
//   a background layer in the window's color fills the rest; a surface's image would be
//   stretched to the new size instead.
class CompositionTarget final : public Presenter {
public:
    // Throws elg::GpuError if the interop isn't available (the caller falls back to a swapchain).
    CompositionTarget(const Device& device, void* hwnd, bool transparent);
    ~CompositionTarget() override;

    CompositionTarget(const CompositionTarget&) = delete;
    CompositionTarget& operator=(const CompositionTarget&) = delete;
    CompositionTarget(CompositionTarget&&) = delete;
    CompositionTarget& operator=(CompositionTarget&&) = delete;

    // Paced to the display: first waits until the compositor is ready for another frame.
    // `background` shows where no frame covers the window yet (opaque windows).
    wgpu::TextureView begin_frame(std::uint32_t width, std::uint32_t height,
                                  const std::array<float, 4>& background) override;
    // Has Direct3D copy the frame into the swapchain once the GPU has rendered it, and show it. With
    // shared fences, neither the copy nor the next frame's rendering waits here: each waits for the
    // other on the GPU (ELGSPEC-0023.R5). Without, it waits for the GPU here.
    // `drawn` is what the swapchain presents as changed (Present1's dirty rects).
    void end_frame(bool reads_backdrops, std::span<const Rect> drawn) override;
    // Backdrops aren't read from the shared image: windows that read them draw offscreen.
    [[nodiscard]] wgpu::Texture copy_source() const override { return wgpu::Texture(); }
    [[nodiscard]] wgpu::TextureFormat view_format() const noexcept override {
        return wgpu::TextureFormat::BGRA8UnormSrgb;
    }
    [[nodiscard]] std::uint32_t width() const noexcept override { return width_; }
    [[nodiscard]] std::uint32_t height() const noexcept override { return height_; }

    // Whether the device can do the interop at all.
    static bool supported(const Device& device);

private:
    struct Impl;

    // (Re)creates the images for `width` x `height`. False when there's no area (minimized).
    bool prepare(std::uint32_t width, std::uint32_t height);

    // Begins WebGPU's access to the shared image and returns the view to render into (sRGB,
    // premultiplied alpha); with shared fences, its rendering waits for Direct3D's last copy from
    // it. end_access() once the frame's rendering is submitted: false if WebGPU refused, else
    // `state` has the fences its rendering signals.
    wgpu::TextureView begin_access();
    bool end_access(wgpu::SharedTextureMemoryEndAccessState& state) noexcept;
    void end_access() noexcept;

    // Sets up the fences Direct3D and WebGPU wait for each other with: false if either can't
    // share them (then the render thread waits).
    bool share_fences();
    // The Direct3D side of a fence WebGPU's rendering signals, opened the first time.
    ID3D11Fence* opened(const wgpu::SharedFence& fence);

    // Waits (up to `timeout_ms`) until the compositor is ready for another frame: paces frames
    // to the display, with at most two queued.
    void wait_until_ready(unsigned timeout_ms);
    // The color shown where no frame covers the window yet (opaque windows; sRGB).
    void set_background(const std::array<float, 4>& srgb);
    // Copies the image into the swapchain and shows it, `drawn` as the changed part: Direct3D's
    // context must wait for the rendering first (on the GPU, or the render thread for it).
    void present(std::span<const Rect> drawn);
    // Without shared fences: waits until Direct3D's copy into the swapchain is done.
    void wait_for_copy();

    void destroy_image() noexcept;

    const Device& device_;
    std::unique_ptr<Impl> impl_;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    bool presented_ = false; // the swapchain's images, as they are now, showed a frame
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

#endif // ELG_GPU_WINDOWS_COMPOSITION_H
