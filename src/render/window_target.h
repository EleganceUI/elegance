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
#include "gpu/composition_win32.h"
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
    gpu::BlurRenderer& blur;
    gpu::PathRenderer& paths;
    VkDescriptorSetLayout image_layout; // one combined image sampler (atlas, layers)
    VkDescriptorSet atlas;
};

// Everything the render thread needs to draw into one window: its swapchain plus the
// per-frame-in-flight command buffers, sync objects, buffers, and pooled offscreen images.
class WindowTarget {
public:
    // `native_window` (an HWND on Windows) lets transparent windows present through the
    // platform compositor where Vulkan swapchains can't be transparent.
    WindowTarget(const gpu::Device& device, VkSurfaceKHR surface, bool transparent = false,
                 void* native_window = nullptr);
    ~WindowTarget();

    WindowTarget(const WindowTarget&) = delete;
    WindowTarget& operator=(const WindowTarget&) = delete;

    // Renders and presents the frame. Does nothing while the window is minimized.
    void render(const WindowFrame& frame, const RenderResources& resources);

private:
    struct FrameSlot {
        VkCommandPool pool = VK_NULL_HANDLE;
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        VkFence in_flight = VK_NULL_HANDLE;
        VkSemaphore image_acquired = VK_NULL_HANDLE;
        gpu::Buffer instances;
        gpu::Buffer stops;
        gpu::Buffer path_vertices;
        std::vector<VkDescriptorPool> descriptor_pools; // reset every frame
        // Offscreen images, reused across frames once this slot's fence has signaled.
        std::vector<gpu::RenderImage> layers;
        std::vector<gpu::RenderImage> blur_horizontal;
        std::vector<gpu::RenderImage> blur_vertical;
        // Path masks: multisampled color and stencil scratch (the masks themselves are cached).
        std::vector<gpu::RenderImage> path_samples;
        std::vector<gpu::RenderImage> path_stencils;
    };

    struct LayerSets {
        VkDescriptorSet content = VK_NULL_HANDLE;
        VkDescriptorSet shadow = VK_NULL_HANDLE;
    };

    // Where a frame goes: a swapchain image, or the composition target's shared image.
    struct DrawTarget {
        VkImage image;
        VkImageView view;
        VkFormat format;
        VkExtent2D extent;
        bool shared; // released to Direct3D (GENERAL, external queue family) instead of presented
    };

    void render_composition(const WindowFrame& frame, const RenderResources& resources);
    void record(FrameSlot& slot, const DrawTarget& target, const WindowFrame& frame, const FramePlan& plan,
                const RenderResources& resources);
    void draw_pass(VkCommandBuffer cmd, const PlanPass& pass, VkFormat format, VkExtent2D extent,
                   const FrameSlot& slot, VkDescriptorSet stops, const std::vector<LayerSets>& layers,
                   const std::vector<VkDescriptorSet>& paths, const RenderResources& resources);
    VkDescriptorSet allocate_set(FrameSlot& slot, VkDescriptorSetLayout layout);
    VkDescriptorSet image_set(FrameSlot& slot, VkDescriptorSetLayout layout, VkImageView view);
    gpu::RenderImage& pooled_image(std::vector<gpu::RenderImage>& pool, std::size_t index, VkFormat format,
                                   std::uint32_t width, std::uint32_t height,
                                   VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT,
                                   VkImageUsageFlags usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                                   VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT);
    void destroy() noexcept;

    // Path coverage masks kept across frames by content (PlanPath::key): unchanged paths are only
    // composited, not rendered again.
    struct CachedMask {
        gpu::RenderImage mask;
        std::uint64_t last_used = 0; // frame number
    };
    const gpu::RenderImage* cached_mask(std::uint64_t key);
    void evict_masks();

    const gpu::Device& device_;
    std::unordered_map<std::uint64_t, CachedMask> masks_;
    std::size_t mask_bytes_ = 0;
    std::uint64_t frame_number_ = 0;
    gpu::Swapchain swapchain_;
#if defined(_WIN32)
    std::unique_ptr<gpu::CompositionTarget> composition_; // transparent windows on Windows
#endif
    VkSampler sampler_ = VK_NULL_HANDLE; // for sampling layers
    std::vector<FrameSlot> slots_;
    std::size_t next_slot_ = 0;
};

} // namespace elg::detail

#endif // ELG_RENDER_WINDOW_TARGET_H
