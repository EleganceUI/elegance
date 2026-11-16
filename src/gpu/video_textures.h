// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_GPU_VIDEO_TEXTURES_H
#define ELG_GPU_VIDEO_TEXTURES_H

#include "gpu/buffer.h"
#include "gpu/device.h"
#include "gpu/eviction.h"
#include "gpu/webgpu.h"
#include "media/video_source.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace elg::detail::gpu {

// What VideoTextures keeps of the textures it made that frames don't draw: the images frames in
// memory are converted into, and Direct3D 11's copies of decoder textures.
inline constexpr CachePolicy kVideoTexturePolicy{std::chrono::seconds(5), std::size_t{128} << 20};

// Video frames' textures (design.md §13.3). Render thread only.
// - Shared textures (VideoFrame::Kind::d3d11_shared): imported into WebGPU once each (shared
//   texture memory) and sampled in place. The decoder writes them outside WebGPU, so every frame
//   that samples one begins access to it first and ends access once submitted.
// - External images (Kind::external): one access per immutable allocation, owned by the
//   source/snapshots. Each recording batch pins that owner through submission; the last
//   snapshot/batch queues asynchronous EndAccess and consumer-fence export, not GPU reuse.
// - Frames in memory (Kind::cpu): written to a storage buffer and turned into a BGRA image by a
//   pass (shaders/video_convert.wgsl) that also converts YUV, so every platform converts the same
//   way. Queue ordering keeps frames in flight intact with one image per source.
class VideoTextures {
public:
    // `policy`: kVideoTexturePolicy, unless tests ask for another.
    VideoTextures(const Device& device, wgpu::BindGroupLayout image_layout,
                  CachePolicy policy = kVideoTexturePolicy);

    VideoTextures(const VideoTextures&) = delete;
    VideoTextures& operator=(const VideoTextures&) = delete;
    VideoTextures(VideoTextures&&) = delete;
    VideoTextures& operator=(VideoTextures&&) = delete;
    ~VideoTextures() = default;

    // The bind group to sample `frame` with: importing its texture the first time and beginning
    // access to it (once per frame: `acquired` lists what this frame has begun), or for frames in
    // memory, recording the upload and conversion into `encoder`. Null if it can't be shown.
    wgpu::BindGroup acquire(const wgpu::CommandEncoder& encoder, const VideoFrame& frame,
                            std::vector<std::uint64_t>& acquired);
    // Starts a window's frame, drawn at `now`: the frames it acquires are the ones it needs.
    void begin_frame(CacheClock::time_point now) noexcept { frame_.begin(now); }

    // Hands `acquired` back to the decoder/producer. Call only after ALL commands recorded
    // using this batch were submitted; release each batch exactly once. Distinct outstanding
    // batches independently pin a texture, even if they sample the same frame. External leases
    // are released after Submit returns; their last owner exports completion fences. If recording
    // or submit throws, leases stay held until this cache is destroyed; never submit that aborted
    // recording afterwards.
    void release(const std::vector<std::uint64_t>& acquired);

    // Ends the frame: releases textures as kVideoTexturePolicy says (a video that changed size,
    // or stopped being shown), except external images a recording still holds.
    void end_frame();

    // How many frame textures (imported or converted into) it holds.
    [[nodiscard]] std::size_t size() const noexcept { return textures_.size() + targets_.size(); }

private:
    struct Texture {
        wgpu::SharedTextureMemory memory;
        wgpu::Texture texture;
        wgpu::Texture copy; // Direct3D 11: sampled instead of `texture` (see import())
        wgpu::BindGroup group;
        std::weak_ptr<SharedTextureOwner> owner;
        std::shared_ptr<const ExternalImage> lease; // external allocation: through submission
        bool external = false; // EndAccess belongs to the last allocation/snapshot owner
        std::size_t pending_batches = 0; // distinct acquire/release batches, not draw count
        CacheUse use;                    // bytes: `copy`'s
    };
    bool import(const VideoFrame& frame, Texture& texture);
    // The frame's shared fence to wait on, imported once.
    wgpu::SharedFence fence(const VideoFrame& frame);

    // Frames in memory: per source, the converted image and its upload buffer.
    struct CpuTarget {
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        wgpu::Texture texture;
        wgpu::TextureView render_view; // UNORM: the pass writes gamma-encoded values
        wgpu::BindGroup sample_group;  // through an sRGB view: sampling linearizes them
        std::unique_ptr<Buffer> pixels;
        wgpu::BindGroup frame_group; // the storage buffer, for the pass
        WGPUBuffer bound = nullptr;  // the buffer frame_group points at
        std::uint64_t frame_id = 0;
        bool converted = false;
        CacheUse use; // bytes: `texture`'s
    };
    wgpu::BindGroup convert(const wgpu::CommandEncoder& encoder, const VideoFrame& frame);
    void create_target(CpuTarget& target, std::uint32_t width, std::uint32_t height);

    const Device& device_;
    wgpu::BindGroupLayout layout_;
    wgpu::Sampler sampler_;
    std::unordered_map<std::uint64_t, Texture> textures_; // by VideoFrame::texture_id
    struct Fence {
        wgpu::SharedFence fence;
        std::uint64_t last_used = 0; // CacheFrame::number()
    };
    std::unordered_map<std::uint64_t, Fence> fences_; // by VideoFrame::wait_fence_id
    std::unordered_map<std::uint64_t, CpuTarget> targets_; // by VideoFrame::source_id
    wgpu::BindGroupLayout frame_layout_; // one storage buffer
    wgpu::RenderPipeline convert_pipeline_;
    CachePolicy policy_;
    CacheFrame frame_;
    bool warned_ = false;
};

} // namespace elg::detail::gpu

#endif // ELG_GPU_VIDEO_TEXTURES_H
