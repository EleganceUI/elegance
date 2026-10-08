// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_RENDER_WINDOW_TARGET_H
#define ELG_RENDER_WINDOW_TARGET_H

#include "gpu/blur_renderer.h"
#include "gpu/buffer.h"
#include "gpu/device.h"
#include "gpu/quad_renderer.h"
#include "gpu/render_image.h"
#include "gpu/swapchain.h"
#include "render/frame_mailbox.h"
#include "render/frame_plan.h"

#include <cstddef>
#include <vector>

namespace elg::detail {

// Render-thread objects shared by all windows.
struct RenderResources {
    gpu::QuadRenderer& quads;
    gpu::BlurRenderer& blur;
    VkDescriptorSetLayout image_layout; // one combined image sampler (atlas, layers)
    VkDescriptorSet atlas;
};

// Everything the render thread needs to draw into one window: its swapchain plus the
// per-frame-in-flight command buffers, sync objects, buffers, and pooled offscreen images.
class WindowTarget {
public:
    WindowTarget(const gpu::Device& device, VkSurfaceKHR surface);
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
        std::vector<VkDescriptorPool> descriptor_pools; // reset every frame
        // Offscreen images, reused across frames once this slot's fence has signaled.
        std::vector<gpu::RenderImage> layers;
        std::vector<gpu::RenderImage> blur_horizontal;
        std::vector<gpu::RenderImage> blur_vertical;
    };

    struct LayerSets {
        VkDescriptorSet content = VK_NULL_HANDLE;
        VkDescriptorSet shadow = VK_NULL_HANDLE;
    };

    void record(FrameSlot& slot, std::uint32_t image_index, const WindowFrame& frame, const FramePlan& plan,
                const RenderResources& resources);
    void draw_pass(VkCommandBuffer cmd, const PlanPass& pass, VkFormat format, VkExtent2D extent,
                   const FrameSlot& slot, VkDescriptorSet stops, const std::vector<LayerSets>& layers,
                   const RenderResources& resources);
    VkDescriptorSet allocate_set(FrameSlot& slot, VkDescriptorSetLayout layout);
    VkDescriptorSet image_set(FrameSlot& slot, VkDescriptorSetLayout layout, VkImageView view);
    gpu::RenderImage& pooled_image(std::vector<gpu::RenderImage>& pool, std::size_t index, VkFormat format,
                                   std::uint32_t width, std::uint32_t height);
    void destroy() noexcept;

    const gpu::Device& device_;
    gpu::Swapchain swapchain_;
    VkSampler sampler_ = VK_NULL_HANDLE; // for sampling layers
    std::vector<FrameSlot> slots_;
    std::size_t next_slot_ = 0;
};

} // namespace elg::detail

#endif // ELG_RENDER_WINDOW_TARGET_H
