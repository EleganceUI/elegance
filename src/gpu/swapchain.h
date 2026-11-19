// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_GPU_SWAPCHAIN_H
#define ELG_GPU_SWAPCHAIN_H

#include "gpu/device.h"
#include "gpu/presenter.h"
#include "gpu/surface_source.h"
#include "gpu/webgpu.h"

#include <webgpu/webgpu_cpp.h>

#include <array>
#include <cstdint>

namespace elg::detail::gpu {

// A window's surface, configured as a swapchain: 8-bit images rendered through sRGB views,
// presented with FIFO (vsync). When the surface is lost, it makes the window a new one from
// `source`, and the frame goes on with it (ELGSPEC-0025.R1).
class Swapchain final : public Presenter {
public:
    // `transparent`: the window shows through where the image's alpha is below 1 (needs a
    // compositor that blends; otherwise it stays opaque).
    Swapchain(const Device& device, const SurfaceSource& source, bool transparent = false);
    ~Swapchain() override;

    Swapchain(const Swapchain&) = delete;
    Swapchain& operator=(const Swapchain&) = delete;
    Swapchain(Swapchain&&) = delete;
    Swapchain& operator=(Swapchain&&) = delete;

    // The surface's images cover the window: `background` isn't shown.
    wgpu::TextureView begin_frame(std::uint32_t width, std::uint32_t height,
                                  const std::array<float, 4>& background) override;
    // The acquired image when it can be copied from, else null.
    [[nodiscard]] wgpu::Texture copy_source() const override { return copyable_ ? texture_ : wgpu::Texture(); }
    // Once a frame read backdrops, the next ones read them straight from the window's images
    // when the surface allows: no offscreen window, no full-window copy. A WebGPU surface presents
    // its whole image: it takes no drawn area.
    void end_frame(bool reads_backdrops, std::span<const Rect> drawn) override;

    [[nodiscard]] wgpu::TextureFormat view_format() const noexcept override { return view_format_; }
    [[nodiscard]] std::uint32_t width() const noexcept override { return width_; }
    [[nodiscard]] std::uint32_t height() const noexcept override { return height_; }

    // How many surfaces it made for the window: 1, and one more each time one was lost.
    [[nodiscard]] std::uint64_t surfaces_made() const noexcept override { return surfaces_made_; }
    // For tests: the next image it asks the surface for comes back as lost.
    void lose_surface_for_testing() noexcept { lose_next_ = true; }

private:
    // A new surface for the window, with the formats and alpha modes it offers.
    void make_surface();
    // (Re)configures the surface if it's stale or the window's size changed. Returns false when
    // the window has no area (e.g. minimized); there is nothing to present then.
    bool prepare(std::uint32_t width, std::uint32_t height);
    // The image to render the next frame into, or null when the surface must be reconfigured
    // first (skip the frame).
    wgpu::TextureView acquire();
    void present();

    // Asks for images that can be copied from (to read backdrops straight from the window
    // instead of drawing it offscreen), from the next prepare() on, if the surface allows it.
    // Plain images stay the default: some platforms compress them only when nothing else reads them.
    void request_copy_source();

    const Device& device_;
    SurfaceSource source_;
    bool transparent_;
    wgpu::Surface surface_;
    std::uint64_t surfaces_made_ = 0;
    bool lost_ = false;      // the surface was lost: begin_frame() makes a new one
    bool lose_next_ = false; // lose_surface_for_testing()
    wgpu::TextureFormat format_ = wgpu::TextureFormat::Undefined;
    wgpu::TextureFormat view_format_ = wgpu::TextureFormat::Undefined;
    wgpu::CompositeAlphaMode alpha_ = wgpu::CompositeAlphaMode::Auto;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    bool stale_ = true;
    bool can_copy_ = false;   // the surface offers CopySrc images
    bool wants_copy_ = false; // request_copy_source() was called
    bool copyable_ = false;   // the current configuration has CopySrc
    wgpu::Texture texture_;   // the acquired image, until presented
};

} // namespace elg::detail::gpu

#endif // ELG_GPU_SWAPCHAIN_H
