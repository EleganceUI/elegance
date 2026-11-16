// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_MEDIA_VIDEO_FRAME_H
#define ELG_MEDIA_VIDEO_FRAME_H

// Frames of videos, cameras, video feeds and GPU images, as the renderer draws them.

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace elg::detail {

namespace gpu {
class ExternalImage; // the GPU layer's (gpu/external_image.h): opaque here
} // namespace gpu

// The GPU adapter the renderer uses (Windows: its LUID), published by the render thread once its
// device exists. Video decoders must use the same adapter to share frames with it. When the
// renderer can't import shared textures (Dawn's Vulkan backend on Windows), decoders hand over
// `cpu` frames instead.
struct RenderAdapter {
    std::array<std::uint8_t, 8> luid{};
    bool shares_textures = false;
};

// Told when the renderer reads a frame's shared texture (render thread), so its owner knows when
// it may write it again.
class SharedTextureOwner {
public:
    virtual ~SharedTextureOwner() = default;
    virtual void began_reading(std::uint64_t texture_id) = 0;
    // `fence` (Windows: a shared D3D fence's NT handle, valid for the call) reaches `value` once
    // the GPU is done with it; null when there's nothing to wait for.
    virtual void finished_reading(std::uint64_t texture_id, void* fence, std::uint64_t value) = 0;
};

// A frame for the renderer (design.md §13.3). How it gets to the GPU depends on the platform:
// - `d3d11_shared`: a BGRA (or `rgba`) texture the decoder shares (Windows: an NT handle),
//   sampled in place. `texture_id` changes when the texture does. With `wait_fence`, the GPU
//   waits for that shared fence to reach `wait_value` before reading; `owner` hears about reads.
// - `external`: an immutable imported allocation with an open Dawn access and an sRGB view.
//   Snapshots and pending recordings retain it until submission; the last owner ends access and
//   exports consumer fences before returning the allocation to its producer.
// - `cpu`: pixels in memory, in `format`, packed (no row padding), top to bottom. The renderer
//   uploads them and converts YUV to RGB in a GPU pass, the same on every platform. Any
//   decoder or camera can start here.
// Later: macOS IOSurfaces and Linux DMA-BUFs, imported like the D3D11 textures (macOS decodes to
// `cpu` NV12 for now).
struct VideoFrame {
    enum class Kind { d3d11_shared, cpu, external };
    enum class Format : std::uint32_t { bgra = 0, nv12 = 1, yuy2 = 2, i420 = 3, rgba = 4 }; // rgba: texture storage
    enum class Matrix : std::uint32_t { bt601 = 0, bt709 = 1 };

    Kind kind = Kind::cpu;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    // d3d11_shared
    void* shared_handle = nullptr;
    std::uint64_t texture_id = 0;
    void* wait_fence = nullptr;
    std::uint64_t wait_fence_id = 0; // unique to the fence (handle values get reused)
    std::uint64_t wait_value = 0;
    std::weak_ptr<SharedTextureOwner> owner;
    // external: the imported image, with its device, texture and prevalidated sRGB view. It owns
    // one immutable imported allocation and its open Dawn access. Every snapshot and pending
    // recording must retain it until submission.
    std::shared_ptr<const gpu::ExternalImage> external;
    // cpu (format also describes shared/external texture storage)
    Format format = Format::bgra;
    Matrix matrix = Matrix::bt709;
    bool full_range = false; // YUV: 0...255 rather than 16...235
    std::shared_ptr<const std::vector<std::uint8_t>> pixels;
    std::uint64_t source_id = 0; // the renderer keeps a ring of images per source
    std::uint64_t frame_id = 0;  // changes with every new frame
};

// Bytes in a packed frame of `format`.
constexpr std::size_t packed_size(VideoFrame::Format format, std::uint32_t width, std::uint32_t height) {
    const std::size_t pixels = static_cast<std::size_t>(width) * height;
    switch (format) {
    case VideoFrame::Format::rgba:
    case VideoFrame::Format::bgra: return pixels * 4;
    case VideoFrame::Format::yuy2: return pixels * 2;
    case VideoFrame::Format::nv12:
    case VideoFrame::Format::i420: return pixels + 2 * (static_cast<std::size_t>((width + 1) / 2) * ((height + 1) / 2));
    }
    return 0;
}

} // namespace elg::detail

#endif // ELG_MEDIA_VIDEO_FRAME_H
