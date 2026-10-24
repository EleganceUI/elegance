// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_GPU_IMAGE_H
#define ELG_GPU_IMAGE_H

#include <elg/export.h>
#include <elg/geometry.h>
#include <elg/view.h>

#include <cstdint>
#include <memory>
#include <optional>

// WebGPU's opaque C handles. No public dependency on Dawn's C++ wrapper. Include the
// matching Dawn webgpu/webgpu.h when using these handles with the WebGPU C API.
typedef struct WGPUDeviceImpl* WGPUDevice;
typedef struct WGPUQueueImpl* WGPUQueue;
typedef struct WGPUTextureImpl* WGPUTexture;

namespace elg {

namespace detail {
class GpuImageSource;
class VideoSource;
} // namespace detail

// Images produced with the renderer's Dawn/WebGPU device, sampled directly without native
// handle imports or CPU readback, on every supported Dawn backend. This is NOT a portable
// sharing API for another WebGPU device, wgpu-native, OpenGL, Vulkan or Direct3D resources.
// Use the same Dawn runtime/ABI as Elegance; do not install a different Dawn proc table.
//
// On one producer thread:
//   if (const auto frame = image.begin_frame()) {
//       // Encode writes to frame->texture using frame->device, then submit all commands
//       // to frame->queue BEFORE publishing (queue writes must also precede end_frame).
//       image.end_frame(*frame);
//   }
// Queue submission order, not CPU/GPU completion, synchronizes producer and renderer.
class ELG_API GpuImage {
public:
    enum class Origin { top_left, bottom_left };
    static constexpr std::uint32_t kBuffers = 3;

    struct Options {
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        Origin origin = Origin::top_left;
    };

    // One exclusive write reservation. Copying Frame does not create another reservation.
    struct Frame {
        std::uint32_t buffer = 0; // 0 ... kBuffers - 1; stable texture per image/buffer
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        WGPUDevice device = nullptr;
        WGPUQueue queue = nullptr;
        WGPUTexture texture = nullptr;
        std::uint64_t sequence = 0; // opaque reservation token; return unchanged
    };

    explicit GpuImage(Options options);

    // Nonblocking: nothing before a renderer device exists, after its shutdown, for invalid
    // dimensions, while a reservation is outstanding, or while all buffers are reserved for
    // display/recording. Calls on copies of this image may be concurrent; only one write
    // reservation is granted at a time. The reservation's owner must synchronize all writes
    // and submit them before end/cancel; do not end/cancel a frame another thread is writing.
    //
    // Handles are BORROWED: never release/destroy them or destroy the device, and keep the
    // GpuImage alive while using them. Stop and join producers before renderer/application
    // shutdown. A frame does not keep the renderer running; device-loss recovery is unsupported.
    // Do not write a texture outside its begin/end reservation, even if you retain a handle.
    //
    // Textures: 2D RGBA8Unorm, one mip/layer/sample, usage RenderAttachment | TextureBinding |
    // CopySrc | CopyDst | StorageBinding. RGBA8UnormSrgb views are allowed. Stored RGB is
    // sRGB-encoded (renderer samples via an sRGB view); alpha is straight (not premultiplied).
    // Rendering via an sRGB attachment view encodes linear shader RGB automatically. Set that
    // view's usage to RenderAttachment (sRGB cannot inherit StorageBinding). Origin only flips
    // display.
    std::optional<Frame> begin_frame() const;
    // Publish only AFTER submitting all writes to frame.queue. No fence or GPU wait is needed.
    // Never submit further writes to this reservation afterwards. The renderer holds buffers
    // until its reads are submitted to that same queue, so subsequent writes are ordered after
    // those reads, even when the GPU has not finished. Newer unpublished-to-display frames may
    // replace older ones. Foreign, stale, or already-ended frames are ignored.
    void end_frame(const Frame& frame) const;
    // Abandon a reservation (e.g. encoding failed). Any writes already submitted must still be
    // on frame.queue, and no commands referencing this reservation may be submitted afterwards.
    void cancel_frame(const Frame& frame) const;

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
