// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_GPU_IMAGE_H
#define ELG_GPU_IMAGE_H

// Images the app renders with its own GPU code (OpenGL, Vulkan, Direct3D), shown without copies
// (design.md §13.3).
//
//   const GpuImage image({.width = 512, .height = 512, .origin = GpuImage::Origin::bottom_left});
//   GpuImageView(image)                                   // in the UI
//
//   // On the app's render thread, every frame:
//   if (const auto frame = image.begin_frame()) {
//       // Import frame->texture once per frame->buffer, draw into it, then signal frame->fence
//       // to frame->fence_value from the same GPU queue.
//       image.end_frame(*frame);
//   }
//
// The image has a few buffers, so the app draws the next frame while the renderer shows the last.
// Windows shares them as Direct3D 11 textures (DXGI_FORMAT_R8G8B8A8_UNORM, gamma-encoded like
// sRGB) and a shared D3D fence, which OpenGL (GL_EXT_memory_object_win32,
// GL_EXT_semaphore_win32), Vulkan (VK_KHR_external_memory_win32, external semaphores), and
// Direct3D 11/12 can all import. Other platforms don't support it yet: begin_frame() gives
// nothing.

#include <elg/export.h>
#include <elg/geometry.h>
#include <elg/view.h>

#include <array>
#include <cstdint>
#include <memory>
#include <optional>

namespace elg {

namespace detail {
class GpuImageSource;
class VideoSource;
} // namespace detail

class ELG_API GpuImage {
public:
    // Where the app's first row goes. OpenGL draws bottom row first.
    enum class Origin { top_left, bottom_left };
    // How end_frame() learns the drawing is done.
    enum class Sync {
        fence,    // the app signals the frame's fence to fence_value (GPU to GPU, no waiting)
        finished, // the app's GPU work on it has completed (e.g. after glFinish)
    };
    static constexpr std::uint32_t kBuffers = 3;

    struct Options {
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        Origin origin = Origin::top_left;
    };

    // A buffer to draw the next frame into.
    struct Frame {
        std::uint32_t buffer = 0; // 0 ... kBuffers - 1. Its handles never change: import each once.
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        // Windows: NT handles the image owns (don't close them).
        void* texture = nullptr; // ID3D11Texture2D: GL_HANDLE_TYPE_D3D11_IMAGE_EXT, VK_..._D3D11_TEXTURE_BIT
        void* fence = nullptr;   // the image's fence (the same for every frame): GL_HANDLE_TYPE_D3D12_FENCE_EXT,
                                 // VK_..._D3D12_FENCE_BIT
        std::uint64_t fence_value = 0;              // Sync::fence: signal the fence to this when drawn
        std::array<std::uint8_t, 8> adapter_luid{}; // the GPU the renderer uses: draw on the same one
    };

    explicit GpuImage(Options options);

    // A buffer the renderer isn't reading, once the renderer's GPU is known (from the first frame
    // of a window), or nothing: not yet, or not supported here. May wait for the GPU to finish
    // reading the buffer. One thread draws: call begin_frame() and end_frame() from it.
    std::optional<Frame> begin_frame() const;
    // Shows the frame, next time windows showing the image draw.
    void end_frame(const Frame& frame, Sync sync = Sync::fence) const;

    Size size() const noexcept; // pixels
    Origin origin() const noexcept;

    bool operator==(const GpuImage& other) const noexcept { return source_ == other.source_; }

    // Internal: the source the views draw.
    std::shared_ptr<detail::VideoSource> source() const noexcept;

private:
    ELG_PRIVATE_MEMBERS_BEGIN
    std::shared_ptr<detail::GpuImageSource> source_;
    ELG_PRIVATE_MEMBERS_END
};

// The image, fitted into (or filling, clipped to) the space offered, on black. Its ideal size is
// the image's own.
ELG_API View GpuImageView(GpuImage image, ContentMode mode = ContentMode::fit);

} // namespace elg

#endif // ELG_GPU_IMAGE_H
