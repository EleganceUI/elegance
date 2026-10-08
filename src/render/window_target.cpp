// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include "render/window_target.h"

#include "render/color_space.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <span>

namespace elg::detail {

namespace {

constexpr std::size_t kFramesInFlight = 2;
constexpr std::uint64_t kNoTimeout = std::numeric_limits<std::uint64_t>::max();
// Layers hold premultiplied linear color; sRGB storage keeps 8-bit precision where it matters.
constexpr VkFormat kLayerFormat = VK_FORMAT_R8G8B8A8_SRGB;
constexpr VkImageSubresourceRange kColorRange{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

std::uint32_t round_up(std::uint32_t value, std::uint32_t multiple) {
    return (value + multiple - 1) / multiple * multiple;
}

void barrier(VkCommandBuffer cmd, VkImage image, VkImageLayout from, VkImageLayout to,
             VkPipelineStageFlags2 src_stage, VkAccessFlags2 src_access, VkPipelineStageFlags2 dst_stage,
             VkAccessFlags2 dst_access) {
    VkImageMemoryBarrier2 b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    b.srcStageMask = src_stage;
    b.srcAccessMask = src_access;
    b.dstStageMask = dst_stage;
    b.dstAccessMask = dst_access;
    b.oldLayout = from;
    b.newLayout = to;
    b.image = image;
    b.subresourceRange = kColorRange;
    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &b;
    vkCmdPipelineBarrier2(cmd, &dependency);
}

void begin_rendering(VkCommandBuffer cmd, VkImageView view, VkExtent2D extent, VkClearColorValue clear) {
    VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    color.imageView = view;
    color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.clearValue.color = clear;
    VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
    rendering.renderArea = {{0, 0}, extent};
    rendering.layerCount = 1;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachments = &color;
    vkCmdBeginRendering(cmd, &rendering);
}

// Brackets rendering into an offscreen image that is sampled afterwards.
template <class Draw>
void render_offscreen(VkCommandBuffer cmd, const gpu::RenderImage& target, VkExtent2D extent, Draw&& draw) {
    barrier(cmd, target.image(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    begin_rendering(cmd, target.view(), extent, VkClearColorValue{});
    draw();
    vkCmdEndRendering(cmd);
    barrier(cmd, target.image(), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
}

} // namespace

WindowTarget::WindowTarget(const gpu::Device& device, VkSurfaceKHR surface)
    : device_(device), swapchain_(device, surface) {
    const VkDevice dev = device_.handle();
    try {
        VkSamplerCreateInfo sampler_info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        sampler_info.magFilter = VK_FILTER_LINEAR;
        sampler_info.minFilter = VK_FILTER_LINEAR;
        sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        gpu::check(vkCreateSampler(dev, &sampler_info, nullptr, &sampler_), "create the layer sampler");

        slots_.reserve(kFramesInFlight);
        for (std::size_t i = 0; i < kFramesInFlight; ++i) {
            FrameSlot& slot = slots_.emplace_back(FrameSlot{
                .instances = gpu::Buffer(device_.allocator(), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT),
                .stops = gpu::Buffer(device_.allocator(), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT),
            });

            VkCommandPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
            pool_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
            pool_info.queueFamilyIndex = device_.queue_family();
            gpu::check(vkCreateCommandPool(dev, &pool_info, nullptr, &slot.pool), "create a command pool");

            VkCommandBufferAllocateInfo cmd_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
            cmd_info.commandPool = slot.pool;
            cmd_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            cmd_info.commandBufferCount = 1;
            gpu::check(vkAllocateCommandBuffers(dev, &cmd_info, &slot.cmd), "allocate a command buffer");

            // Signaled so the first wait on each slot returns immediately.
            VkFenceCreateInfo fence_info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
            fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
            gpu::check(vkCreateFence(dev, &fence_info, nullptr, &slot.in_flight), "create a fence");

            VkSemaphoreCreateInfo semaphore_info{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
            gpu::check(vkCreateSemaphore(dev, &semaphore_info, nullptr, &slot.image_acquired),
                       "create a semaphore");
        }
    } catch (...) {
        destroy();
        throw;
    }
}

WindowTarget::~WindowTarget() {
    destroy();
}

void WindowTarget::destroy() noexcept {
    const VkDevice dev = device_.handle();
    for (FrameSlot& slot : slots_) {
        if (slot.in_flight) {
            vkWaitForFences(dev, 1, &slot.in_flight, VK_TRUE, kNoTimeout);
            vkDestroyFence(dev, slot.in_flight, nullptr);
        }
        vkDestroySemaphore(dev, slot.image_acquired, nullptr);
        vkDestroyCommandPool(dev, slot.pool, nullptr);
        for (VkDescriptorPool pool : slot.descriptor_pools) {
            vkDestroyDescriptorPool(dev, pool, nullptr);
        }
    }
    slots_.clear();
    vkDestroySampler(dev, sampler_, nullptr);
    sampler_ = VK_NULL_HANDLE;
}

void WindowTarget::render(const WindowFrame& frame, const RenderResources& resources) {
    const VkExtent2D fallback{static_cast<std::uint32_t>(std::max(frame.pixel_width, 0)),
                              static_cast<std::uint32_t>(std::max(frame.pixel_height, 0))};
    if (!swapchain_.prepare(fallback)) {
        return; // minimized
    }

    const VkDevice dev = device_.handle();
    FrameSlot& slot = slots_[next_slot_];
    gpu::check(vkWaitForFences(dev, 1, &slot.in_flight, VK_TRUE, kNoTimeout), "wait for a frame");

    std::uint32_t image_index = 0;
    VkResult acquired = vkAcquireNextImageKHR(dev, swapchain_.handle(), kNoTimeout, slot.image_acquired,
                                              VK_NULL_HANDLE, &image_index);
    if (acquired == VK_ERROR_OUT_OF_DATE_KHR) {
        swapchain_.mark_stale();
        if (!swapchain_.prepare(fallback)) {
            return;
        }
        acquired = vkAcquireNextImageKHR(dev, swapchain_.handle(), kNoTimeout, slot.image_acquired,
                                         VK_NULL_HANDLE, &image_index);
    }
    if (acquired == VK_SUBOPTIMAL_KHR) {
        swapchain_.mark_stale(); // still presentable; recreate next frame
    } else {
        gpu::check(acquired, "acquire a swapchain image");
    }

    // Only reset once we're sure to submit, or the next wait on this fence would never return.
    gpu::check(vkResetFences(dev, 1, &slot.in_flight), "reset a fence");

    const FramePlan plan = plan_frame(frame.list);
    slot.instances.write(std::as_bytes(std::span(plan.instances)));
    slot.stops.write(std::as_bytes(std::span(plan.stops)));
    record(slot, image_index, frame, plan, resources);

    VkSemaphoreSubmitInfo wait{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    wait.semaphore = slot.image_acquired;
    wait.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSemaphoreSubmitInfo signal{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    signal.semaphore = swapchain_.render_finished(image_index);
    signal.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkCommandBufferSubmitInfo cmd_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    cmd_info.commandBuffer = slot.cmd;

    VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    submit.waitSemaphoreInfoCount = 1;
    submit.pWaitSemaphoreInfos = &wait;
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &cmd_info;
    submit.signalSemaphoreInfoCount = 1;
    submit.pSignalSemaphoreInfos = &signal;
    gpu::check(vkQueueSubmit2(device_.queue(), 1, &submit, slot.in_flight), "submit a frame");

    const VkSwapchainKHR swapchain = swapchain_.handle();
    const VkSemaphore render_finished = swapchain_.render_finished(image_index);
    VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &render_finished;
    present.swapchainCount = 1;
    present.pSwapchains = &swapchain;
    present.pImageIndices = &image_index;
    const VkResult presented = vkQueuePresentKHR(device_.queue(), &present);
    if (presented == VK_ERROR_OUT_OF_DATE_KHR || presented == VK_SUBOPTIMAL_KHR) {
        swapchain_.mark_stale();
    } else {
        gpu::check(presented, "present a frame");
    }

    next_slot_ = (next_slot_ + 1) % slots_.size();
}

VkDescriptorSet WindowTarget::allocate_set(FrameSlot& slot, VkDescriptorSetLayout layout) {
    VkDescriptorSetAllocateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    info.descriptorSetCount = 1;
    info.pSetLayouts = &layout;
    VkDescriptorSet set = VK_NULL_HANDLE;
    for (VkDescriptorPool pool : slot.descriptor_pools) {
        info.descriptorPool = pool;
        const VkResult result = vkAllocateDescriptorSets(device_.handle(), &info, &set);
        if (result == VK_SUCCESS) {
            return set;
        }
        if (result != VK_ERROR_OUT_OF_POOL_MEMORY && result != VK_ERROR_FRAGMENTED_POOL) {
            gpu::throw_vk_error(result, "allocate a descriptor set");
        }
    }

    // All pools are full: add one.
    const VkDescriptorPoolSize sizes[] = {
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 64},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 8},
    };
    VkDescriptorPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool_info.maxSets = 72;
    pool_info.poolSizeCount = 2;
    pool_info.pPoolSizes = sizes;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    gpu::check(vkCreateDescriptorPool(device_.handle(), &pool_info, nullptr, &pool), "create a descriptor pool");
    slot.descriptor_pools.push_back(pool);
    info.descriptorPool = pool;
    gpu::check(vkAllocateDescriptorSets(device_.handle(), &info, &set), "allocate a descriptor set");
    return set;
}

VkDescriptorSet WindowTarget::image_set(FrameSlot& slot, VkDescriptorSetLayout layout, VkImageView view) {
    const VkDescriptorSet set = allocate_set(slot, layout);
    const VkDescriptorImageInfo image{sampler_, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = set;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &image;
    vkUpdateDescriptorSets(device_.handle(), 1, &write, 0, nullptr);
    return set;
}

gpu::RenderImage& WindowTarget::pooled_image(std::vector<gpu::RenderImage>& pool, std::size_t index,
                                             VkFormat format, std::uint32_t width, std::uint32_t height) {
    // Sizes are rounded up so small changes (animations, resizes) reuse the same image.
    if (index < pool.size() && pool[index].width() >= width && pool[index].height() >= height) {
        return pool[index];
    }
    std::uint32_t capacity_width = round_up(width, 256);
    std::uint32_t capacity_height = round_up(height, 256);
    if (index < pool.size()) {
        capacity_width = std::max(capacity_width, pool[index].width());
        capacity_height = std::max(capacity_height, pool[index].height());
        pool[index] = gpu::RenderImage(device_, format, capacity_width, capacity_height);
        return pool[index];
    }
    return pool.emplace_back(device_, format, capacity_width, capacity_height);
}

void WindowTarget::draw_pass(VkCommandBuffer cmd, const PlanPass& pass, VkFormat format, VkExtent2D extent,
                             const FrameSlot& slot, VkDescriptorSet stops, const std::vector<LayerSets>& layers,
                             const RenderResources& resources) {
    resources.quads.begin(cmd, format, extent, slot.instances.handle(), resources.atlas, stops);
    for (const PlanSegment& segment : pass.segments) {
        if (segment.layer < 0) {
            resources.quads.draw(cmd, segment.first, segment.count);
            continue;
        }
        const LayerSets& sets = layers[static_cast<std::size_t>(segment.layer)];
        const VkDescriptorSet image = segment.shadow ? sets.shadow : sets.content;
        if (!image) {
            continue;
        }
        resources.quads.bind_image(cmd, image);
        resources.quads.draw(cmd, segment.first, segment.count);
        resources.quads.bind_image(cmd, resources.atlas);
    }
}

void WindowTarget::record(FrameSlot& slot, std::uint32_t image_index, const WindowFrame& frame,
                          const FramePlan& plan, const RenderResources& resources) {
    const VkDevice dev = device_.handle();
    gpu::check(vkResetCommandPool(dev, slot.pool, 0), "reset a command pool");
    for (VkDescriptorPool pool : slot.descriptor_pools) {
        gpu::check(vkResetDescriptorPool(dev, pool, 0), "reset a descriptor pool");
    }

    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    gpu::check(vkBeginCommandBuffer(slot.cmd, &begin), "begin a command buffer");
    const VkCommandBuffer cmd = slot.cmd;

    // Gradient stops for every pass.
    const VkDescriptorSet stops = allocate_set(slot, resources.quads.stops_layout());
    const VkDescriptorBufferInfo stops_buffer{slot.stops.handle(), 0, VK_WHOLE_SIZE};
    VkWriteDescriptorSet stops_write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    stops_write.dstSet = stops;
    stops_write.dstBinding = 0;
    stops_write.descriptorCount = 1;
    stops_write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    stops_write.pBufferInfo = &stops_buffer;
    vkUpdateDescriptorSets(dev, 1, &stops_write, 0, nullptr);

    // Offscreen layers first, innermost first; each may also get a two-pass blurred shadow.
    std::vector<LayerSets> layer_sets(plan.layers.size());
    std::size_t shadows = 0;
    for (std::size_t i = 0; i < plan.layers.size(); ++i) {
        const PlanPass& layer = plan.layers[i];
        const VkExtent2D extent{layer.width, layer.height};

        const gpu::RenderImage& content = pooled_image(slot.layers, i, kLayerFormat, layer.width, layer.height);
        render_offscreen(cmd, content, extent, [&] {
            draw_pass(cmd, layer, kLayerFormat, extent, slot, stops, layer_sets, resources);
        });
        layer_sets[i].content = image_set(slot, resources.image_layout, content.view());

        if (layer.shadow_sigma) {
            const float sigma = *layer.shadow_sigma;
            const gpu::RenderImage& horizontal =
                pooled_image(slot.blur_horizontal, shadows, gpu::BlurRenderer::kFormat, layer.width, layer.height);
            render_offscreen(cmd, horizontal, extent, [&] {
                resources.blur.blur(cmd, layer_sets[i].content, layer.width, layer.height, true, sigma,
                                    gpu::BlurRenderer::Channel::alpha);
            });
            const VkDescriptorSet horizontal_set = image_set(slot, resources.image_layout, horizontal.view());

            const gpu::RenderImage& vertical =
                pooled_image(slot.blur_vertical, shadows, gpu::BlurRenderer::kFormat, layer.width, layer.height);
            render_offscreen(cmd, vertical, extent, [&] {
                resources.blur.blur(cmd, horizontal_set, layer.width, layer.height, false, sigma,
                                    gpu::BlurRenderer::Channel::red);
            });
            layer_sets[i].shadow = image_set(slot, resources.image_layout, vertical.view());
            ++shadows;
        }
    }

    // The window itself.
    const VkImage image = swapchain_.image(image_index);
    // The acquire semaphore is waited at COLOR_ATTACHMENT_OUTPUT, so the layout transition
    // starts at that stage too. The previous contents are discarded (UNDEFINED).
    barrier(cmd, image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_NONE,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    const auto clear = to_linear_premultiplied(frame.list.clear_color);
    begin_rendering(cmd, swapchain_.view(image_index), swapchain_.extent(),
                    VkClearColorValue{{clear[0], clear[1], clear[2], clear[3]}});
    draw_pass(cmd, plan.main, swapchain_.format(), swapchain_.extent(), slot, stops, layer_sets, resources);
    vkCmdEndRendering(cmd);
    barrier(cmd, image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE);

    gpu::check(vkEndCommandBuffer(cmd), "end a command buffer");
}

} // namespace elg::detail
