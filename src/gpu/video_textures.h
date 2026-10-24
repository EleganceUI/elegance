// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_GPU_VIDEO_TEXTURES_H
#define ELG_GPU_VIDEO_TEXTURES_H

#include "gpu/buffer.h"
#include "gpu/device.h"
#include "gpu/webgpu.h"
#include "media/video_source.h"

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace elg::detail::gpu {

// Video frames' textures (design.md §13.3). Render thread only.
// - Same-device WebGPU textures (Kind::webgpu): sampled directly. Keep the snapshot's lease
//   through submission; subsequent producer writes on the same queue follow these reads.
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
    VideoTextures(const Device& device, const wgpu::BindGroupLayout& image_layout);

    VideoTextures(const VideoTextures&) = delete;
    VideoTextures& operator=(const VideoTextures&) = delete;

    // The bind group to sample `frame` with: importing its texture the first time and beginning
    // access to it (once per frame: `acquired` lists what this frame has begun), or for frames in
    // memory, recording the upload and conversion into `encoder`. Null if it can't be shown.
    wgpu::BindGroup acquire(const wgpu::CommandEncoder& encoder, const VideoFrame& frame,
                            std::vector<std::uint64_t>& acquired);
    // Hands `acquired` back to the decoder/producer. Call only after ALL commands recorded
    // using this batch were submitted; release each batch exactly once. Distinct outstanding
    // batches independently pin a texture, even if they sample the same frame. A WebGPU lease
    // is released after Submit returns, not GPU completion: later producer writes must use the
    // same queue. If recording or
    // submit throws, leases stay held until this cache is destroyed; never submit that aborted
    // recording afterwards.
    void release(const std::vector<std::uint64_t>& acquired);

    // Once per rendered frame: drops textures no frame has used for a while (a video that
    // changed size, or stopped being shown).
    void end_frame() noexcept;

private:
    struct Texture {
        wgpu::SharedTextureMemory memory;
        wgpu::Texture texture;
        wgpu::Texture copy; // Direct3D 11: sampled instead of `texture` (see import())
        wgpu::BindGroup group;
        std::weak_ptr<SharedTextureOwner> owner;
        std::shared_ptr<void> lease; // WebGPU reservation / external allocation: through submission
        bool external = false; // EndAccess belongs to the last allocation/snapshot owner
        std::size_t pending_batches = 0; // distinct acquire/release batches, not draw count
        std::uint64_t last_used = 0;
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
        std::uint64_t last_used = 0;
    };
    wgpu::BindGroup convert(const wgpu::CommandEncoder& encoder, const VideoFrame& frame);
    void create_target(CpuTarget& target, std::uint32_t width, std::uint32_t height);

    const Device& device_;
    wgpu::BindGroupLayout layout_;
    wgpu::Sampler sampler_;
    std::unordered_map<std::uint64_t, Texture> textures_; // by VideoFrame::texture_id
    struct Fence {
        wgpu::SharedFence fence;
        std::uint64_t last_used = 0;
    };
    std::unordered_map<std::uint64_t, Fence> fences_; // by VideoFrame::wait_fence_id
    std::unordered_map<std::uint64_t, CpuTarget> targets_; // by VideoFrame::source_id
    wgpu::BindGroupLayout frame_layout_; // one storage buffer
    wgpu::RenderPipeline convert_pipeline_;
    std::uint64_t frame_ = 0;
    bool warned_ = false;
};

} // namespace elg::detail::gpu

#endif // ELG_GPU_VIDEO_TEXTURES_H
