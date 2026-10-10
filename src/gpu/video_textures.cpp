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
constexpr VkFormat kConvertedFormat = VK_FORMAT_B8G8R8A8_UNORM;

constexpr std::uint32_t kConvertVertSpirv[] = {
#include "shaders/blur.vert.inc"
};
constexpr std::uint32_t kConvertFragSpirv[] = {
#include "shaders/video_convert.frag.inc"
};

struct ConvertPush {
    std::uint32_t format;
    std::uint32_t width;
    std::uint32_t height;
    std::uint32_t matrix;
    std::uint32_t full_range;
};

void image_barrier(VkCommandBuffer cmd, VkImage image, VkImageLayout from, VkImageLayout to,
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
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &b;
    vkCmdPipelineBarrier2(cmd, &dependency);
}

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
    create_converter();
#if defined(_WIN32)
    get_handle_properties_ = reinterpret_cast<void*>(vkGetDeviceProcAddr(dev, "vkGetMemoryWin32HandlePropertiesKHR"));
#endif
}

VideoTextures::~VideoTextures() {
    vkDeviceWaitIdle(device_.handle());
    for (auto& [id, texture] : textures_) {
        destroy(texture);
    }
    for (auto& [id, ring] : rings_) {
        for (CpuSlot& slot : ring.slots) {
            destroy(slot);
        }
    }
    const VkDevice dev = device_.handle();
    vkDestroyPipeline(dev, convert_pipeline_, nullptr);
    vkDestroyPipelineLayout(dev, convert_layout_, nullptr);
    vkDestroyShaderModule(dev, convert_vertex_, nullptr);
    vkDestroyShaderModule(dev, convert_fragment_, nullptr);
    vkDestroyDescriptorPool(dev, frame_pool_, nullptr);
    vkDestroyDescriptorSetLayout(dev, frame_layout_, nullptr);
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
    if (frame.kind == VideoFrame::Kind::cpu) {
        return convert(cmd, frame);
    }
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
    std::erase_if(rings_, [&](auto& entry) {
        if (frame_ - entry.second.last_used > kIdleFrames) {
            for (CpuSlot& slot : entry.second.slots) {
                destroy(slot);
            }
            return true;
        }
        return false;
    });
}

// --- Frames in memory ---

void VideoTextures::create_converter() {
    const VkDevice dev = device_.handle();
    const auto module = [&](const std::uint32_t* code, std::size_t size) {
        VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        info.codeSize = size;
        info.pCode = code;
        VkShaderModule created = VK_NULL_HANDLE;
        check(vkCreateShaderModule(dev, &info, nullptr, &created), "create a shader module");
        return created;
    };
    convert_vertex_ = module(kConvertVertSpirv, sizeof(kConvertVertSpirv));
    convert_fragment_ = module(kConvertFragSpirv, sizeof(kConvertFragSpirv));

    VkDescriptorSetLayoutBinding binding{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo layout_info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layout_info.bindingCount = 1;
    layout_info.pBindings = &binding;
    check(vkCreateDescriptorSetLayout(dev, &layout_info, nullptr, &frame_layout_), "create a descriptor set layout");
    const VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, kMaxSets};
    VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool.maxSets = kMaxSets;
    pool.poolSizeCount = 1;
    pool.pPoolSizes = &size;
    check(vkCreateDescriptorPool(dev, &pool, nullptr, &frame_pool_), "create a descriptor pool");

    const VkPushConstantRange push{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(ConvertPush)};
    VkPipelineLayoutCreateInfo pipeline_layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pipeline_layout.setLayoutCount = 1;
    pipeline_layout.pSetLayouts = &frame_layout_;
    pipeline_layout.pushConstantRangeCount = 1;
    pipeline_layout.pPushConstantRanges = &push;
    check(vkCreatePipelineLayout(dev, &pipeline_layout, nullptr, &convert_layout_), "create a pipeline layout");

    const VkPipelineShaderStageCreateInfo stages[] = {
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, convert_vertex_,
         "main", nullptr},
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT,
         convert_fragment_, "main", nullptr},
    };
    VkPipelineVertexInputStateCreateInfo vertex_input{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo input_assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState blend{};
    blend.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo color_blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    color_blend.attachmentCount = 1;
    color_blend.pAttachments = &blend;
    const VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dynamic_states;
    const VkFormat format = kConvertedFormat;
    VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachmentFormats = &format;
    VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    info.pNext = &rendering;
    info.stageCount = 2;
    info.pStages = stages;
    info.pVertexInputState = &vertex_input;
    info.pInputAssemblyState = &input_assembly;
    info.pViewportState = &viewport;
    info.pRasterizationState = &raster;
    info.pMultisampleState = &multisample;
    info.pColorBlendState = &color_blend;
    info.pDynamicState = &dynamic;
    info.layout = convert_layout_;
    check(vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &info, nullptr, &convert_pipeline_),
          "create the video conversion pipeline");
}

bool VideoTextures::create_slot(CpuSlot& slot, std::uint32_t width, std::uint32_t height) {
    const VkDevice dev = device_.handle();
    VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    info.flags = VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = kConvertedFormat;
    info.extent = {width, height, 1};
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    VmaAllocationCreateInfo allocation{};
    allocation.usage = VMA_MEMORY_USAGE_AUTO;
    if (vmaCreateImage(device_.allocator(), &info, &allocation, &slot.image, &slot.allocation, nullptr) != VK_SUCCESS) {
        return false;
    }
    VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view.image = slot.image;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    view.format = kConvertedFormat;
    if (vkCreateImageView(dev, &view, nullptr, &slot.render_view) != VK_SUCCESS) {
        return false;
    }
    view.format = VK_FORMAT_B8G8R8A8_SRGB;
    if (vkCreateImageView(dev, &view, nullptr, &slot.sample_view) != VK_SUCCESS) {
        return false;
    }
    // Sampling: from the shared pool of image sets (or a freed one).
    if (!free_sets_.empty()) {
        slot.sample_set = free_sets_.back();
        free_sets_.pop_back();
    } else {
        VkDescriptorSetAllocateInfo set_info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        set_info.descriptorPool = pool_;
        set_info.descriptorSetCount = 1;
        set_info.pSetLayouts = &layout_;
        if (vkAllocateDescriptorSets(dev, &set_info, &slot.sample_set) != VK_SUCCESS) {
            slot.sample_set = VK_NULL_HANDLE;
            return false;
        }
    }
    const VkDescriptorImageInfo image{sampler_, slot.sample_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = slot.sample_set;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &image;
    vkUpdateDescriptorSets(dev, 1, &write, 0, nullptr);
    VkDescriptorSetAllocateInfo frame_info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    frame_info.descriptorPool = frame_pool_;
    frame_info.descriptorSetCount = 1;
    frame_info.pSetLayouts = &frame_layout_;
    if (vkAllocateDescriptorSets(dev, &frame_info, &slot.frame_set) != VK_SUCCESS) {
        slot.frame_set = VK_NULL_HANDLE;
        return false;
    }
    slot.pixels = std::make_unique<Buffer>(device_.allocator(), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    return true;
}

void VideoTextures::destroy(CpuSlot& slot) noexcept {
    const VkDevice dev = device_.handle();
    if (slot.sample_set) {
        free_sets_.push_back(slot.sample_set);
    }
    if (slot.frame_set) {
        vkFreeDescriptorSets(dev, frame_pool_, 1, &slot.frame_set);
    }
    vkDestroyImageView(dev, slot.render_view, nullptr);
    vkDestroyImageView(dev, slot.sample_view, nullptr);
    if (slot.image) {
        vmaDestroyImage(device_.allocator(), slot.image, slot.allocation);
    }
    slot = {};
}

VkDescriptorSet VideoTextures::convert(VkCommandBuffer cmd, const VideoFrame& frame) {
    if (!frame.pixels || frame.width == 0 || frame.height == 0 ||
        frame.pixels->size() < packed_size(frame.format, frame.width, frame.height)) {
        return VK_NULL_HANDLE;
    }
    CpuRing& ring = rings_[frame.source_id];
    ring.last_used = frame_;
    if (ring.width != frame.width || ring.height != frame.height) {
        // Waiting is rare (a camera or video changed size) and keeps the ring simple.
        vkDeviceWaitIdle(device_.handle());
        for (CpuSlot& slot : ring.slots) {
            destroy(slot);
        }
        ring = {};
        ring.last_used = frame_;
        for (CpuSlot& slot : ring.slots) {
            if (!create_slot(slot, frame.width, frame.height)) {
                log_warning("video: can't make images for a {}x{} frame", frame.width, frame.height);
                return VK_NULL_HANDLE;
            }
        }
        ring.width = frame.width;
        ring.height = frame.height;
    }
    if (ring.current >= 0 && ring.frame_id == frame.frame_id) {
        return ring.slots[ring.current].sample_set; // converted already (another window this frame)
    }

    // The next slot: older frames in flight still read the others.
    const int index = (ring.current + 1) % static_cast<int>(ring.slots.size());
    CpuSlot& slot = ring.slots[index];
    slot.pixels->write(std::as_bytes(std::span(*frame.pixels)));
    if (slot.bound != slot.pixels->handle()) {
        const VkDescriptorBufferInfo buffer{slot.pixels->handle(), 0, VK_WHOLE_SIZE};
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = slot.frame_set;
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        write.pBufferInfo = &buffer;
        vkUpdateDescriptorSets(device_.handle(), 1, &write, 0, nullptr);
        slot.bound = slot.pixels->handle();
    }

    image_barrier(cmd, slot.image, slot.used ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
                  VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                  VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                  VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    VkRenderingAttachmentInfo attachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    attachment.imageView = slot.render_view;
    attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
    rendering.renderArea = {{0, 0}, {frame.width, frame.height}};
    rendering.layerCount = 1;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachments = &attachment;
    vkCmdBeginRendering(cmd, &rendering);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, convert_pipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, convert_layout_, 0, 1, &slot.frame_set, 0, nullptr);
    const ConvertPush push{static_cast<std::uint32_t>(frame.format), frame.width, frame.height,
                           static_cast<std::uint32_t>(frame.matrix), frame.full_range ? 1u : 0u};
    vkCmdPushConstants(cmd, convert_layout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
    const VkViewport viewport{0, 0, static_cast<float>(frame.width), static_cast<float>(frame.height), 0, 1};
    const VkRect2D scissor{{0, 0}, {frame.width, frame.height}};
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdEndRendering(cmd);
    image_barrier(cmd, slot.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                  VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                  VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    slot.used = true;
    ring.current = index;
    ring.frame_id = frame.frame_id;
    return slot.sample_set;
}

} // namespace elg::detail::gpu
