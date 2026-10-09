// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include "gpu/video_textures.h"

#include "core/log.h"

#if defined(_WIN32)
#include <windows.h>
#include <vulkan/vulkan_win32.h>
#endif

namespace elg::detail::gpu {

namespace {

constexpr std::uint64_t kIdleFrames = 240;
constexpr std::uint32_t kMaxSets = 64;

void barrier(VkCommandBuffer cmd, VkImage image, VkImageLayout from, VkImageLayout to, std::uint32_t src_family,
             std::uint32_t dst_family, VkPipelineStageFlags2 src_stage, VkAccessFlags2 src_access,
             VkPipelineStageFlags2 dst_stage, VkAccessFlags2 dst_access) {
    VkImageMemoryBarrier2 b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    b.srcStageMask = src_stage;
    b.srcAccessMask = src_access;
    b.dstStageMask = dst_stage;
    b.dstAccessMask = dst_access;
    b.oldLayout = from;
    b.newLayout = to;
    b.srcQueueFamilyIndex = src_family;
    b.dstQueueFamilyIndex = dst_family;
    b.image = image;
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &b;
    vkCmdPipelineBarrier2(cmd, &dependency);
}

} // namespace

bool VideoTextures::supported(const Device& device) {
#if defined(_WIN32)
    return device.has_extension("VK_KHR_external_memory_win32");
#else
    (void)device;
    return false;
#endif
}

VideoTextures::VideoTextures(const Device& device, VkDescriptorSetLayout image_layout)
    : device_(device), layout_(image_layout) {
    const VkDevice dev = device_.handle();
    VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sampler.magFilter = VK_FILTER_LINEAR;
    sampler.minFilter = VK_FILTER_LINEAR;
    sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    check(vkCreateSampler(dev, &sampler, nullptr, &sampler_), "create the video sampler");
    const VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kMaxSets};
    VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool.maxSets = kMaxSets;
    pool.poolSizeCount = 1;
    pool.pPoolSizes = &size;
    check(vkCreateDescriptorPool(dev, &pool, nullptr, &pool_), "create the video descriptor pool");
#if defined(_WIN32)
    get_handle_properties_ = reinterpret_cast<void*>(vkGetDeviceProcAddr(dev, "vkGetMemoryWin32HandlePropertiesKHR"));
#endif
}

VideoTextures::~VideoTextures() {
    vkDeviceWaitIdle(device_.handle());
    for (auto& [id, texture] : textures_) {
        destroy(texture);
    }
    vkDestroyDescriptorPool(device_.handle(), pool_, nullptr);
    vkDestroySampler(device_.handle(), sampler_, nullptr);
}

void VideoTextures::destroy(Texture& texture) noexcept {
    const VkDevice dev = device_.handle();
    if (texture.set) {
        free_sets_.push_back(texture.set);
    }
    vkDestroyImageView(dev, texture.view, nullptr);
    vkDestroyImage(dev, texture.image, nullptr);
    vkFreeMemory(dev, texture.memory, nullptr);
    texture = {};
}

bool VideoTextures::import(const VideoFrame& frame, Texture& texture) {
#if defined(_WIN32)
    const VkDevice dev = device_.handle();
    const auto get_properties = reinterpret_cast<PFN_vkGetMemoryWin32HandlePropertiesKHR>(get_handle_properties_);
    if (!get_properties || !frame.shared_handle) {
        return false;
    }
    VkExternalMemoryImageCreateInfo external{VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO};
    external.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT;
    VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    info.pNext = &external;
    info.flags = VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT; // UNORM storage, sampled through an sRGB view
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = VK_FORMAT_B8G8R8A8_UNORM;
    info.extent = {frame.width, frame.height, 1};
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(dev, &info, nullptr, &texture.image) != VK_SUCCESS) {
        return false;
    }
    VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements(dev, texture.image, &requirements);
    VkMemoryWin32HandlePropertiesKHR properties{VK_STRUCTURE_TYPE_MEMORY_WIN32_HANDLE_PROPERTIES_KHR};
    if (get_properties(dev, VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT, frame.shared_handle, &properties) !=
        VK_SUCCESS) {
        destroy(texture);
        return false;
    }
    const std::uint32_t allowed = requirements.memoryTypeBits & properties.memoryTypeBits;
    VkPhysicalDeviceMemoryProperties memory;
    vkGetPhysicalDeviceMemoryProperties(device_.physical(), &memory);
    std::uint32_t type = UINT32_MAX;
    for (std::uint32_t i = 0; i < memory.memoryTypeCount; ++i) {
        if ((allowed & (1u << i)) &&
            (type == UINT32_MAX || (memory.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))) {
            type = i;
        }
    }
    if (type == UINT32_MAX) {
        destroy(texture);
        return false;
    }
    VkImportMemoryWin32HandleInfoKHR import_info{VK_STRUCTURE_TYPE_IMPORT_MEMORY_WIN32_HANDLE_INFO_KHR};
    import_info.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT;
    import_info.handle = static_cast<HANDLE>(frame.shared_handle);
    VkMemoryDedicatedAllocateInfo dedicated{VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO};
    dedicated.pNext = &import_info;
    dedicated.image = texture.image;
    VkMemoryAllocateInfo allocate{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocate.pNext = &dedicated;
    allocate.allocationSize = requirements.size;
    allocate.memoryTypeIndex = type;
    if (vkAllocateMemory(dev, &allocate, nullptr, &texture.memory) != VK_SUCCESS ||
        vkBindImageMemory(dev, texture.image, texture.memory, 0) != VK_SUCCESS) {
        destroy(texture);
        return false;
    }
    VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view.image = texture.image;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = VK_FORMAT_B8G8R8A8_SRGB;
    view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (vkCreateImageView(dev, &view, nullptr, &texture.view) != VK_SUCCESS) {
        destroy(texture);
        return false;
    }
    if (!free_sets_.empty()) {
        texture.set = free_sets_.back();
        free_sets_.pop_back();
    } else {
        VkDescriptorSetAllocateInfo set_info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        set_info.descriptorPool = pool_;
        set_info.descriptorSetCount = 1;
        set_info.pSetLayouts = &layout_;
        if (vkAllocateDescriptorSets(dev, &set_info, &texture.set) != VK_SUCCESS) {
            texture.set = VK_NULL_HANDLE;
            destroy(texture);
            return false;
        }
    }
    const VkDescriptorImageInfo descriptor{sampler_, texture.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = texture.set;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &descriptor;
    vkUpdateDescriptorSets(dev, 1, &write, 0, nullptr);
    return true;
#else
    (void)frame;
    (void)texture;
    return false;
#endif
}

VkDescriptorSet VideoTextures::acquire(VkCommandBuffer cmd, const VideoFrame& frame, std::vector<VkImage>& acquired) {
    auto it = textures_.find(frame.texture_id);
    if (it == textures_.end()) {
        Texture texture;
        if (!import(frame, texture)) {
            log_warning("video: can't import a frame texture into Vulkan");
            return VK_NULL_HANDLE;
        }
        it = textures_.emplace(frame.texture_id, texture).first;
    }
    Texture& texture = it->second;
    texture.last_used = frame_;
    if (std::ranges::find(acquired, texture.image) == acquired.end()) {
        // Written by the decoder (outside Vulkan): take it from the external queue family. Its
        // contents are defined by the decoder, so the old layout is GENERAL, never UNDEFINED.
        barrier(cmd, texture.image, texture.used ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_QUEUE_FAMILY_EXTERNAL, device_.queue_family(), VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE,
                VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        acquired.push_back(texture.image);
        texture.used = true;
    }
    return texture.set;
}

void VideoTextures::release(VkCommandBuffer cmd, const std::vector<VkImage>& acquired) const {
    for (VkImage image : acquired) {
        barrier(cmd, image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL, device_.queue_family(),
                VK_QUEUE_FAMILY_EXTERNAL, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE);
    }
}

void VideoTextures::end_frame() noexcept {
    ++frame_;
    std::erase_if(textures_, [&](auto& entry) {
        if (frame_ - entry.second.last_used > kIdleFrames) {
            destroy(entry.second);
            return true;
        }
        return false;
    });
}

} // namespace elg::detail::gpu
