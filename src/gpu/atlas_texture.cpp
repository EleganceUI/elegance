// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include "gpu/atlas_texture.h"

#include <cstring>
#include <limits>
#include <vector>

namespace elg::detail::gpu {

namespace {

constexpr VkFormat kAtlasFormat = VK_FORMAT_R8G8B8A8_UNORM;
constexpr VkImageSubresourceRange kColorRange{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

VkImageMemoryBarrier2 layout_barrier(VkImage image, VkImageLayout from, VkImageLayout to,
                                     VkPipelineStageFlags2 src_stage, VkAccessFlags2 src_access,
                                     VkPipelineStageFlags2 dst_stage, VkAccessFlags2 dst_access) {
    VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    barrier.srcStageMask = src_stage;
    barrier.srcAccessMask = src_access;
    barrier.dstStageMask = dst_stage;
    barrier.dstAccessMask = dst_access;
    barrier.oldLayout = from;
    barrier.newLayout = to;
    barrier.image = image;
    barrier.subresourceRange = kColorRange;
    return barrier;
}

void apply(VkCommandBuffer cmd, const VkImageMemoryBarrier2& barrier) {
    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(cmd, &dependency);
}

} // namespace

AtlasTexture::AtlasTexture(const Device& device)
    : device_(device), staging_(device.allocator(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT) {
    const VkDevice dev = device_.handle();
    try {
        VkImageCreateInfo image_info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        image_info.imageType = VK_IMAGE_TYPE_2D;
        image_info.format = kAtlasFormat;
        image_info.extent = {kAtlasSize, kAtlasSize, 1};
        image_info.mipLevels = 1;
        image_info.arrayLayers = 1;
        image_info.samples = VK_SAMPLE_COUNT_1_BIT;
        image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
        image_info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        VmaAllocationCreateInfo allocation_info{};
        allocation_info.usage = VMA_MEMORY_USAGE_AUTO;
        check(vmaCreateImage(device_.allocator(), &image_info, &allocation_info, &image_, &allocation_, nullptr),
              "create the glyph atlas");

        VkImageViewCreateInfo view_info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view_info.image = image_;
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_info.format = kAtlasFormat;
        view_info.subresourceRange = kColorRange;
        check(vkCreateImageView(dev, &view_info, nullptr, &view_), "create the glyph atlas view");

        // Linear filtering is what makes distance fields scale smoothly.
        VkSamplerCreateInfo sampler_info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        sampler_info.magFilter = VK_FILTER_LINEAR;
        sampler_info.minFilter = VK_FILTER_LINEAR;
        sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        check(vkCreateSampler(dev, &sampler_info, nullptr, &sampler_), "create the glyph atlas sampler");

        VkDescriptorSetLayoutBinding binding{};
        binding.binding = 0;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        binding.descriptorCount = 1;
        binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorSetLayoutCreateInfo layout_info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        layout_info.bindingCount = 1;
        layout_info.pBindings = &binding;
        check(vkCreateDescriptorSetLayout(dev, &layout_info, nullptr, &set_layout_), "create a descriptor set layout");

        VkDescriptorPoolSize pool_size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1};
        VkDescriptorPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pool_info.maxSets = 1;
        pool_info.poolSizeCount = 1;
        pool_info.pPoolSizes = &pool_size;
        check(vkCreateDescriptorPool(dev, &pool_info, nullptr, &descriptor_pool_), "create a descriptor pool");

        VkDescriptorSetAllocateInfo set_info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        set_info.descriptorPool = descriptor_pool_;
        set_info.descriptorSetCount = 1;
        set_info.pSetLayouts = &set_layout_;
        check(vkAllocateDescriptorSets(dev, &set_info, &descriptor_set_), "allocate a descriptor set");

        const VkDescriptorImageInfo image_descriptor{sampler_, view_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = descriptor_set_;
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &image_descriptor;
        vkUpdateDescriptorSets(dev, 1, &write, 0, nullptr);

        VkCommandPoolCreateInfo command_pool_info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        command_pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        command_pool_info.queueFamilyIndex = device_.queue_family();
        check(vkCreateCommandPool(dev, &command_pool_info, nullptr, &command_pool_), "create a command pool");
        VkCommandBufferAllocateInfo cmd_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        cmd_info.commandPool = command_pool_;
        cmd_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cmd_info.commandBufferCount = 1;
        check(vkAllocateCommandBuffers(dev, &cmd_info, &cmd_), "allocate a command buffer");
        VkFenceCreateInfo fence_info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        check(vkCreateFence(dev, &fence_info, nullptr, &fence_), "create a fence");

        // Start cleared and in the layout the shader expects, so sampling before the first
        // upload is valid.
        submit([&](VkCommandBuffer cmd) {
            apply(cmd, layout_barrier(image_, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                      VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_CLEAR_BIT,
                                      VK_ACCESS_2_TRANSFER_WRITE_BIT));
            const VkClearColorValue zero{};
            vkCmdClearColorImage(cmd, image_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &zero, 1, &kColorRange);
            apply(cmd, layout_barrier(image_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_CLEAR_BIT,
                                      VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                                      VK_ACCESS_2_SHADER_SAMPLED_READ_BIT));
        });
    } catch (...) {
        destroy();
        throw;
    }
}

AtlasTexture::~AtlasTexture() {
    destroy();
}

void AtlasTexture::destroy() noexcept {
    const VkDevice dev = device_.handle();
    vkDestroyFence(dev, fence_, nullptr);
    vkDestroyCommandPool(dev, command_pool_, nullptr);
    vkDestroyDescriptorPool(dev, descriptor_pool_, nullptr);
    vkDestroyDescriptorSetLayout(dev, set_layout_, nullptr);
    vkDestroySampler(dev, sampler_, nullptr);
    vkDestroyImageView(dev, view_, nullptr);
    if (image_) {
        vmaDestroyImage(device_.allocator(), image_, allocation_);
    }
    fence_ = VK_NULL_HANDLE;
    command_pool_ = VK_NULL_HANDLE;
    descriptor_pool_ = VK_NULL_HANDLE;
    set_layout_ = VK_NULL_HANDLE;
    sampler_ = VK_NULL_HANDLE;
    view_ = VK_NULL_HANDLE;
    image_ = VK_NULL_HANDLE;
}

void AtlasTexture::upload(std::span<const GlyphUpload> uploads) {
    if (uploads.empty()) {
        return;
    }

    // Pack all glyphs into the staging buffer (RGBA8 texels keep every offset 4-byte aligned).
    std::size_t total = 0;
    for (const GlyphUpload& upload : uploads) {
        total += upload.rgba.size();
    }
    std::vector<std::byte> packed(total);
    std::vector<VkBufferImageCopy> regions;
    regions.reserve(uploads.size());
    std::size_t offset = 0;
    for (const GlyphUpload& upload : uploads) {
        std::memcpy(packed.data() + offset, upload.rgba.data(), upload.rgba.size());
        VkBufferImageCopy region{};
        region.bufferOffset = offset;
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageOffset = {static_cast<std::int32_t>(upload.x), static_cast<std::int32_t>(upload.y), 0};
        region.imageExtent = {upload.width, upload.height, 1};
        regions.push_back(region);
        offset += upload.rgba.size();
    }

    // Frames in flight may be sampling the atlas; let them finish before changing its layout.
    check(vkQueueWaitIdle(device_.queue()), "wait for the GPU before a glyph upload");
    staging_.write(packed);

    submit([&](VkCommandBuffer cmd) {
        apply(cmd, layout_barrier(image_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                                  VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, VK_PIPELINE_STAGE_2_COPY_BIT,
                                  VK_ACCESS_2_TRANSFER_WRITE_BIT));
        vkCmdCopyBufferToImage(cmd, staging_.handle(), image_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               static_cast<std::uint32_t>(regions.size()), regions.data());
        apply(cmd, layout_barrier(image_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT,
                                  VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                                  VK_ACCESS_2_SHADER_SAMPLED_READ_BIT));
    });
}

void AtlasTexture::submit(const std::function<void(VkCommandBuffer)>& record) {
    const VkDevice dev = device_.handle();
    check(vkResetCommandBuffer(cmd_, 0), "reset a command buffer");
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(vkBeginCommandBuffer(cmd_, &begin), "begin a command buffer");
    record(cmd_);
    check(vkEndCommandBuffer(cmd_), "end a command buffer");

    VkCommandBufferSubmitInfo cmd_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    cmd_info.commandBuffer = cmd_;
    VkSubmitInfo2 submit_info{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    submit_info.commandBufferInfoCount = 1;
    submit_info.pCommandBufferInfos = &cmd_info;
    check(vkQueueSubmit2(device_.queue(), 1, &submit_info, fence_), "submit a glyph upload");
    check(vkWaitForFences(dev, 1, &fence_, VK_TRUE, std::numeric_limits<std::uint64_t>::max()),
          "wait for a glyph upload");
    check(vkResetFences(dev, 1, &fence_), "reset a fence");
}

} // namespace elg::detail::gpu
