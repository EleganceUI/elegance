// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_GPU_PRESENTER_H
#define ELG_GPU_PRESENTER_H

#include "gpu/webgpu_types.h"

#include <array>
#include <cstdint>
#include <memory>

namespace elg::detail::gpu {

class Device;

// Where a window's frames go: its WebGPU surface (Swapchain), or the platform's compositor where
// windows present through it. Each frame begins with begin_frame(), which readies an image to
// render into, and ends with end_frame(), which shows it. Render thread only.
class Presenter {
public:
    Presenter() = default;
    Presenter(const Presenter&) = delete;
    Presenter& operator=(const Presenter&) = delete;
    Presenter(Presenter&&) = delete;
    Presenter& operator=(Presenter&&) = delete;
    virtual ~Presenter() = default;

    // Readies an image of `width` x `height` pixels for the next frame and returns the view to
    // render into, or null when there is nothing to present (the window has no area) or the frame
    // must be skipped. `background` (sRGB) is the window's color, for presenters that show it
    // wherever no frame covers the window yet.
    virtual wgpu::TextureView begin_frame(std::uint32_t width, std::uint32_t height,
                                          const std::array<float, 4>& background) = 0;
    // The format of the views begin_frame() returns (sRGB), and the size of its image.
    [[nodiscard]] virtual wgpu::TextureFormat view_format() const noexcept = 0;
    [[nodiscard]] virtual std::uint32_t width() const noexcept = 0;
    [[nodiscard]] virtual std::uint32_t height() const noexcept = 0;
    // The image begin_frame() readied when it can be copied from, else null.
    [[nodiscard]] virtual wgpu::Texture copy_source() const = 0;
    // Shows the frame rendered into the image, once its commands were submitted.
    // `reads_backdrops`: the frame read backdrops, which later frames then read from the image
    // directly if the presenter can make it copyable.
    virtual void end_frame(bool reads_backdrops) = 0;
};

// The presenter for a window with `surface`, transparent or not. `native_window` (an HWND on
// Windows) lets it present through the platform's compositor, which can show transparency. Each
// platform has its own (gpu/windows/composition.cpp; gpu/presenter.cpp elsewhere).
std::unique_ptr<Presenter> create_presenter(const Device& device, wgpu::Surface surface, bool transparent,
                                            void* native_window);

} // namespace elg::detail::gpu

#endif // ELG_GPU_PRESENTER_H
