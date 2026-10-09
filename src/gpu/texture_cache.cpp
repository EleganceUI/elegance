// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include "gpu/texture_cache.h"

#include <algorithm>
#include <bit>

namespace elg::detail::gpu {

namespace {

constexpr VkFormat kFormat = VK_FORMAT_R8G8B8A8_SRGB; // sampling yields linear color
// Dropped when unused for this many frames (of any window), or sooner when over the budget.
constexpr std::uint64_t kIdleFrames = 600;
// Frames that may still be in flight using a texture: never drop one used more recently.
constexpr std::uint64_t kInFlightMargin = 16;
constexpr std::size_t kBudgetBytes = 512ull << 20;
constexpr std::uint32_t kSetsPerPool = 64;

void barrier(VkCommandBuffer cmd, VkImage image, std::uint32_t level, std::uint32_t levels, VkImageLayout from,
             VkImageLayout to, VkPipelineStageFlags2 src_stage, VkAccessFlags2 src_access,
             VkPipelineStageFlags2 dst_stage, VkAccessFlags2 dst_access) {
    VkImageMemoryBarrier2 b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    b.srcStageMask = src_stage;
    b.srcAccessMask = src_access;
    b.dstStageMask = dst_stage;
    b.dstAccessMask = dst_access;
    b.oldLayout = from;
    b.newLayout = to;
    b.image = image;
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, level, levels, 0, 1};
    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &b;
    vkCmdPipelineBarrier2(cmd, &dependency);
}

} // namespace

TextureCache::TextureCache(const Device& device, VkDescriptorSetLayout image_layout)
    : device_(device), layout_(image_layout) {
    VkSamplerCreateInfo info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    info.magFilter = VK_FILTER_LINEAR;
    info.minFilter = VK_FILTER_LINEAR;
    info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    info.maxLod = VK_LOD_CLAMP_NONE;
    check(vkCreateSampler(device_.handle(), &info, nullptr, &sampler_), "create the image sampler");
}

TextureCache::~TextureCache() {
    vkDeviceWaitIdle(device_.handle());
    for (auto& [id, texture] : textures_) {
        destroy(texture);
    }
    for (VkDescriptorPool pool : pools_) {
        vkDestroyDescriptorPool(device_.handle(), pool, nullptr);
    }
    vkDestroySampler(device_.handle(), sampler_, nullptr);
}

void TextureCache::destroy(Texture& texture) noexcept {
    if (texture.set) {
        free_sets_.push_back(texture.set);
    }
    vkDestroyImageView(device_.handle(), texture.view, nullptr);
    if (texture.image) {
        vmaDestroyImage(device_.allocator(), texture.image, texture.allocation);
    }
    bytes_ -= texture.bytes;
    texture = {};
}

VkDescriptorSet TextureCache::allocate_set() {
    if (!free_sets_.empty()) {
        const VkDescriptorSet set = free_sets_.back();
        free_sets_.pop_back();
        return set;
    }
    VkDescriptorSetAllocateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    info.descriptorSetCount = 1;
    info.pSetLayouts = &layout_;
    VkDescriptorSet set = VK_NULL_HANDLE;
    if (!pools_.empty()) {
        info.descriptorPool = pools_.back();
        if (vkAllocateDescriptorSets(device_.handle(), &info, &set) == VK_SUCCESS) {
            return set;
        }
    }
    const VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kSetsPerPool};
    VkDescriptorPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool_info.maxSets = kSetsPerPool;
    pool_info.poolSizeCount = 1;
    pool_info.pPoolSizes = &size;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    check(vkCreateDescriptorPool(device_.handle(), &pool_info, nullptr, &pool), "create a descriptor pool");
    pools_.push_back(pool);
    info.descriptorPool = pool;
    check(vkAllocateDescriptorSets(device_.handle(), &info, &set), "allocate an image descriptor set");
    return set;
}

VkDescriptorSet TextureCache::acquire(VkCommandBuffer cmd, const DecodedImage& image, std::vector<Buffer>& keep_alive) {
    if (const auto it = textures_.find(image.id); it != textures_.end()) {
        it->second.last_used = frame_;
        return it->second.set;
    }
    const VkDevice dev = device_.handle();
    const std::uint32_t levels = std::bit_width(std::max(image.width, image.height));

    Texture texture;
    VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = kFormat;
    info.extent = {image.width, image.height, 1};
    info.mipLevels = levels;
    info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VmaAllocationCreateInfo allocation_info{};
    allocation_info.usage = VMA_MEMORY_USAGE_AUTO;
    check(vmaCreateImage(device_.allocator(), &info, &allocation_info, &texture.image, &texture.allocation, nullptr),
          "create an image texture");
    try {
        VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view.image = texture.image;
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = kFormat;
        view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, levels, 0, 1};
        check(vkCreateImageView(dev, &view, nullptr, &texture.view), "create an image texture view");
        texture.set = allocate_set();
    } catch (...) {
        destroy(texture);
        throw;
    }
    const VkDescriptorImageInfo descriptor{sampler_, texture.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = texture.set;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &descriptor;
    vkUpdateDescriptorSets(dev, 1, &write, 0, nullptr);

    // Level 0 from a staging buffer, then each level blitted from the one above.
    Buffer& staging = keep_alive.emplace_back(device_.allocator(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    staging.write(std::as_bytes(std::span(image.rgba)));
    barrier(cmd, texture.image, 0, levels, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {image.width, image.height, 1};
    vkCmdCopyBufferToImage(cmd, staging.handle(), texture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    std::int32_t w = static_cast<std::int32_t>(image.width);
    std::int32_t h = static_cast<std::int32_t>(image.height);
    for (std::uint32_t level = 1; level < levels; ++level) {
        barrier(cmd, texture.image, level - 1, 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
        const std::int32_t next_w = std::max(1, w / 2);
        const std::int32_t next_h = std::max(1, h / 2);
        VkImageBlit blit{};
        blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level - 1, 0, 1};
        blit.srcOffsets[1] = {w, h, 1};
        blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level, 0, 1};
        blit.dstOffsets[1] = {next_w, next_h, 1};
        vkCmdBlitImage(cmd, texture.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, texture.image,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
        w = next_w;
        h = next_h;
    }
    // Every level ends up readable by fragment shaders (the last one is still a transfer target).
    if (levels > 1) {
        barrier(cmd, texture.image, 0, levels - 1, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                VK_ACCESS_2_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    }
    barrier(cmd, texture.image, levels - 1, 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

    texture.bytes = image.rgba.size() * 4 / 3; // with mips
    texture.last_used = frame_;
    bytes_ += texture.bytes;
    return textures_.emplace(image.id, texture).first->second.set;
}

void TextureCache::end_frame() noexcept {
    ++frame_;
    std::erase_if(textures_, [&](auto& entry) {
        Texture& texture = entry.second;
        const std::uint64_t idle = frame_ - texture.last_used;
        if (idle > kIdleFrames) {
            destroy(texture);
            return true;
        }
        return false;
    });
    // Over budget: the least recently used first, never one a frame in flight may still use.
    while (bytes_ > kBudgetBytes) {
        auto oldest = textures_.end();
        for (auto it = textures_.begin(); it != textures_.end(); ++it) {
            if (oldest == textures_.end() || it->second.last_used < oldest->second.last_used) {
                oldest = it;
            }
        }
        if (oldest == textures_.end() || frame_ - oldest->second.last_used <= kInFlightMargin) {
            break;
        }
        destroy(oldest->second);
        textures_.erase(oldest);
    }
}

} // namespace elg::detail::gpu
