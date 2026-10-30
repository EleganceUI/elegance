// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_RENDER_WINDOW_TARGET_H
#define ELG_RENDER_WINDOW_TARGET_H

#include "gpu/blur_renderer.h"
#include "gpu/buffer.h"
#include "gpu/path_renderer.h"
#include "gpu/device.h"
#include "gpu/quad_renderer.h"
#include "gpu/render_image.h"
#if defined(_WIN32)
#include "platform/windows/composition.h"
#endif
#include "gpu/texture_cache.h"
#include "gpu/video_textures.h"
#include "gpu/swapchain.h"
#include "render/frame_mailbox.h"
#include "render/frame_plan.h"

#include <cstddef>
#include <unordered_map>
#include <memory>
#include <optional>
#include <vector>

namespace elg::detail {

// Render-thread objects shared by all windows.
struct RenderResources {
    gpu::QuadRenderer& quads;
    gpu::TextureCache& textures;
    gpu::VideoTextures& videos;
    gpu::BlurRenderer& blur;
    gpu::PathRenderer& paths;
    wgpu::BindGroupLayout image_layout; // a texture and its sampler (atlas, layers)
    wgpu::BindGroup atlas;
};

// Everything the render thread needs to draw into one window: its surface (or composition
// target), the frame's buffers, and pooled offscreen images. WebGPU orders queue writes and
// submissions, so one set serves every frame, even while earlier ones are still on the GPU.
class WindowTarget {
public:
    // `native_window` (an HWND on Windows) lets windows present through the platform
    // compositor, which can show transparency (WebGPU surfaces there can't).
    WindowTarget(const gpu::Device& device, wgpu::Surface surface, bool transparent = false,
                 void* native_window = nullptr);

    WindowTarget(const WindowTarget&) = delete;
    WindowTarget& operator=(const WindowTarget&) = delete;

    // Renders and presents the frame. Does nothing while the window is minimized.
    void render(const WindowFrame& frame, const RenderResources& resources);

private:
    struct LayerGroups {
        wgpu::BindGroup content; // blurred, when the layer blurs
        wgpu::BindGroup raw;     // as drawn
        wgpu::BindGroup shadow;
        wgpu::BindGroup inner_shadow;
    };

    // Where a frame goes: a surface image, or the composition target's shared image. `texture`
    // is set when it can be copied from: backdrops are then read from it directly, instead of
    // the window drawing offscreen and being copied over.
    struct DrawTarget {
        wgpu::TextureView view;
        wgpu::TextureFormat format;
        std::uint32_t width;
        std::uint32_t height;
        wgpu::Texture texture;
    };

    // What a pass draws into. `texture` is set when it can be copied from (layers, the offscreen
    // window image): only such passes can read backdrops.
    struct PassTarget {
        wgpu::Texture texture;
        wgpu::TextureView view;
        wgpu::TextureFormat format;
        std::uint32_t width;
        std::uint32_t height;
        wgpu::Color clear;
    };

    // The images and groups a frame's passes sample.
    struct FrameGroups {
        wgpu::BindGroup stops;
        std::vector<LayerGroups> layers;
        std::vector<wgpu::BindGroup> paths;
        std::vector<wgpu::BindGroup> images;
        std::vector<wgpu::BindGroup> videos;
    };

    void render_composition(const WindowFrame& frame, const RenderResources& resources);
    // Records the frame. `acquired_videos` collects the shared video textures it samples, to
    // release once it's submitted.
    wgpu::CommandBuffer record(const DrawTarget& target, const WindowFrame& frame, const RenderResources& resources,
                               std::vector<std::uint64_t>& acquired_videos);
    // Records a pass, split wherever its instances read a backdrop.
    void draw_pass(const wgpu::CommandEncoder& encoder, const PassTarget& target, const PlanPass& plan_pass,
                   const FramePlan& plan, const FrameGroups& groups, const RenderResources& resources);
    // Copies a backdrop's region out of `texture` and blurs it as planned; returns its group.
    wgpu::BindGroup take_backdrop(const wgpu::CommandEncoder& encoder, const wgpu::Texture& texture,
                                  const PlanBackdrop& backdrop, std::size_t slot, const RenderResources& resources);
    // Blurs a color image (`width` x `height` of it) into pooled images, downsampled by `factor`
    // first; `slot` picks the pooled images. Returns the result's group.
    wgpu::BindGroup blur_color(const wgpu::CommandEncoder& encoder, const wgpu::BindGroup& source, std::uint32_t width,
                               std::uint32_t height, float sigma, std::uint32_t factor, bool clamp_edges,
                               std::size_t slot, const RenderResources& resources);
    const wgpu::BindGroup& empty_group(const RenderResources& resources);
    wgpu::BindGroup image_group(const RenderResources& resources, const wgpu::TextureView& view) const;
    gpu::RenderImage& pooled_image(std::vector<gpu::RenderImage>& pool, std::size_t index, wgpu::TextureFormat format,
                                   std::uint32_t width, std::uint32_t height, std::uint32_t samples = 1,
                                   wgpu::TextureUsage usage = wgpu::TextureUsage::RenderAttachment |
                                                              wgpu::TextureUsage::TextureBinding);

    // Path coverage masks kept across frames by content (PlanPath::key): unchanged paths are only
    // composited, not rendered again.
    struct CachedMask {
        gpu::RenderImage mask;
        wgpu::BindGroup group;
        std::uint64_t last_used = 0; // frame number
    };
    const CachedMask* cached_mask(std::uint64_t key);
    void evict_masks();

    const gpu::Device& device_;
    std::unordered_map<std::uint64_t, CachedMask> masks_;
    std::size_t mask_bytes_ = 0;
    std::uint64_t frame_number_ = 0;
    std::unique_ptr<gpu::Swapchain> swapchain_; // unless the window presents through composition
#if defined(_WIN32)
    std::unique_ptr<gpu::CompositionTarget> composition_;
#endif
    wgpu::Sampler sampler_; // for sampling layers
    gpu::Buffer instances_;
    gpu::Buffer stops_;
    gpu::Buffer path_vertices_;
    // Offscreen images, reused across frames.
    std::vector<gpu::RenderImage> layers_;
    std::vector<gpu::RenderImage> blur_horizontal_;
    std::vector<gpu::RenderImage> blur_vertical_;
    std::vector<gpu::RenderImage> inner_horizontal_;
    std::vector<gpu::RenderImage> inner_vertical_;
    // Color blurs (blurred layers by layer index, then backdrops): downsampled, then two passes.
    std::vector<gpu::RenderImage> color_down_;
    std::vector<gpu::RenderImage> color_horizontal_;
    std::vector<gpu::RenderImage> color_vertical_;
    std::vector<gpu::RenderImage> backdrop_copies_;
    std::vector<gpu::RenderImage> window_copies_; // backdrops read from the surface image (its format)
    std::vector<gpu::RenderImage> frame_images_;  // the window, drawn offscreen when it reads backdrops
    bool window_reads_backdrops_ = false;         // the last frame recorded did
    std::optional<gpu::RenderImage> empty_;      // bound where no second image is
    wgpu::BindGroup empty_group_;
    // Path masks: multisampled color and stencil scratch (the masks themselves are cached).
    std::vector<gpu::RenderImage> path_samples_;
    std::vector<gpu::RenderImage> path_stencils_;
};

} // namespace elg::detail

#endif // ELG_RENDER_WINDOW_TARGET_H
