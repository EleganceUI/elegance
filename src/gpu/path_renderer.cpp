// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include "gpu/path_renderer.h"

#include "core/log.h"

#include <elg/error.h>

#include <cstddef>
#include <iterator>

namespace elg::detail::gpu {

namespace {

constexpr std::uint32_t kPathVertSpirv[] = {
#include "shaders/path.vert.inc"
};
constexpr std::uint32_t kPathCoverVertSpirv[] = {
#include "shaders/path_cover.vert.inc"
};
constexpr std::uint32_t kPathFragSpirv[] = {
#include "shaders/path.frag.inc"
};

struct PushConstants {
    float viewport[2];
};

VkShaderModule create_module(VkDevice device, const std::uint32_t* code, std::size_t size) {
    VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    info.codeSize = size;
    info.pCode = code;
    VkShaderModule module = VK_NULL_HANDLE;
    check(vkCreateShaderModule(device, &info, nullptr, &module), "create a shader module");
    return module;
}

bool has_depth(VkFormat format) {
    return format == VK_FORMAT_D24_UNORM_S8_UINT || format == VK_FORMAT_D32_SFLOAT_S8_UINT ||
           format == VK_FORMAT_D16_UNORM_S8_UINT;
}

void image_barrier(VkCommandBuffer cmd, VkImage image, VkImageAspectFlags aspect, VkImageLayout from, VkImageLayout to,
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
    b.subresourceRange = {aspect, 0, 1, 0, 1};
    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &b;
    vkCmdPipelineBarrier2(cmd, &dependency);
}

} // namespace

PathRenderer::PathRenderer(const Device& device) : device_(device) {
    // A stencil format the GPU can render to (not every GPU has every one: AMD lacks D24S8).
    for (const VkFormat candidate : {VK_FORMAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D32_SFLOAT_S8_UINT}) {
        VkFormatProperties properties{};
        vkGetPhysicalDeviceFormatProperties(device.physical(), candidate, &properties);
        if (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
            stencil_format_ = candidate;
            break;
        }
    }
    if (stencil_format_ == VK_FORMAT_UNDEFINED) {
        throw GpuError("the GPU has no stencil format to draw paths with");
    }
    stencil_aspect_ = has_depth(stencil_format_) ? VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT
                                                 : VK_IMAGE_ASPECT_STENCIL_BIT;

    // The most samples (anti-aliasing levels) both attachments support, up to 8.
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(device.physical(), &properties);
    const VkSampleCountFlags supported =
        properties.limits.framebufferColorSampleCounts & properties.limits.framebufferStencilSampleCounts;
    for (const VkSampleCountFlagBits count : {VK_SAMPLE_COUNT_8_BIT, VK_SAMPLE_COUNT_4_BIT, VK_SAMPLE_COUNT_2_BIT}) {
        if (supported & count) {
            samples_ = count;
            break;
        }
    }
    log_debug("paths: stencil format {}, {}x MSAA", static_cast<int>(stencil_format_), static_cast<int>(samples_));

    try {
        fill_vertex_ = create_module(device_.handle(), kPathVertSpirv, sizeof(kPathVertSpirv));
        cover_vertex_ = create_module(device_.handle(), kPathCoverVertSpirv, sizeof(kPathCoverVertSpirv));
        fragment_ = create_module(device_.handle(), kPathFragSpirv, sizeof(kPathFragSpirv));
        VkPushConstantRange push{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstants)};
        VkPipelineLayoutCreateInfo layout_info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        layout_info.pushConstantRangeCount = 1;
        layout_info.pPushConstantRanges = &push;
        check(vkCreatePipelineLayout(device_.handle(), &layout_info, nullptr, &layout_), "create the path pipeline layout");
        stencil_non_zero_ = create_pipeline(false, false);
        stencil_even_odd_ = create_pipeline(false, true);
        cover_non_zero_ = create_pipeline(true, false);
        cover_even_odd_ = create_pipeline(true, true);
    } catch (...) {
        destroy();
        throw;
    }
}

PathRenderer::~PathRenderer() {
    destroy();
}

void PathRenderer::destroy() noexcept {
    const VkDevice device = device_.handle();
    for (VkPipeline* pipeline : {&stencil_non_zero_, &stencil_even_odd_, &cover_non_zero_, &cover_even_odd_}) {
        vkDestroyPipeline(device, *pipeline, nullptr);
        *pipeline = VK_NULL_HANDLE;
    }
    vkDestroyPipelineLayout(device, layout_, nullptr);
    vkDestroyShaderModule(device, fragment_, nullptr);
    vkDestroyShaderModule(device, cover_vertex_, nullptr);
    vkDestroyShaderModule(device, fill_vertex_, nullptr);
    layout_ = VK_NULL_HANDLE;
    fragment_ = cover_vertex_ = fill_vertex_ = VK_NULL_HANDLE;
}

VkPipeline PathRenderer::create_pipeline(bool cover, bool even_odd) {
    const VkPipelineShaderStageCreateInfo stages[] = {
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT,
         cover ? cover_vertex_ : fill_vertex_, "main", nullptr},
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fragment_,
         "main", nullptr},
    };

    const VkVertexInputBindingDescription binding{0, 2 * sizeof(float), VK_VERTEX_INPUT_RATE_VERTEX};
    const VkVertexInputAttributeDescription attribute{0, 0, VK_FORMAT_R32G32_SFLOAT, 0};
    VkPipelineVertexInputStateCreateInfo vertex_input{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    if (!cover) {
        vertex_input.vertexBindingDescriptionCount = 1;
        vertex_input.pVertexBindingDescriptions = &binding;
        vertex_input.vertexAttributeDescriptionCount = 1;
        vertex_input.pVertexAttributeDescriptions = &attribute;
    }

    VkPipelineInputAssemblyStateCreateInfo input_assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE; // both facings count: that's the winding
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisample.rasterizationSamples = samples_;

    VkPipelineDepthStencilStateCreateInfo depth_stencil{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depth_stencil.stencilTestEnable = VK_TRUE;
    VkStencilOpState front{};
    VkStencilOpState back{};
    if (!cover) {
        // Count windings: +1 for triangles one way round, -1 the other (non-zero), or flip the
        // low bit (even-odd).
        front = {VK_STENCIL_OP_KEEP,
                 even_odd ? VK_STENCIL_OP_INVERT : VK_STENCIL_OP_INCREMENT_AND_WRAP,
                 VK_STENCIL_OP_KEEP,
                 VK_COMPARE_OP_ALWAYS,
                 0xFF,
                 0xFF,
                 0};
        back = front;
        back.passOp = even_odd ? VK_STENCIL_OP_INVERT : VK_STENCIL_OP_DECREMENT_AND_WRAP;
    } else {
        // Inside where the count is non-zero (or odd).
        front = {VK_STENCIL_OP_KEEP, VK_STENCIL_OP_KEEP,  VK_STENCIL_OP_KEEP, VK_COMPARE_OP_NOT_EQUAL,
                 even_odd ? 1u : 0xFFu, 0, 0};
        back = front;
    }
    depth_stencil.front = front;
    depth_stencil.back = back;

    VkPipelineColorBlendAttachmentState blend{};
    blend.colorWriteMask = cover ? VK_COLOR_COMPONENT_R_BIT : 0;
    VkPipelineColorBlendStateCreateInfo color_blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    color_blend.attachmentCount = 1;
    color_blend.pAttachments = &blend;

    const VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount = static_cast<std::uint32_t>(std::size(dynamic_states));
    dynamic.pDynamicStates = dynamic_states;

    const VkFormat color_format = kMaskFormat;
    VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachmentFormats = &color_format;
    rendering.depthAttachmentFormat = has_depth(stencil_format_) ? stencil_format_ : VK_FORMAT_UNDEFINED;
    rendering.stencilAttachmentFormat = stencil_format_;

    VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    info.pNext = &rendering;
    info.stageCount = static_cast<std::uint32_t>(std::size(stages));
    info.pStages = stages;
    info.pVertexInputState = &vertex_input;
    info.pInputAssemblyState = &input_assembly;
    info.pViewportState = &viewport;
    info.pRasterizationState = &raster;
    info.pMultisampleState = &multisample;
    info.pDepthStencilState = &depth_stencil;
    info.pColorBlendState = &color_blend;
    info.pDynamicState = &dynamic;
    info.layout = layout_;
    VkPipeline pipeline = VK_NULL_HANDLE;
    check(vkCreateGraphicsPipelines(device_.handle(), VK_NULL_HANDLE, 1, &info, nullptr, &pipeline),
          "create a path pipeline");
    return pipeline;
}

void PathRenderer::render(VkCommandBuffer cmd, const RenderImage& samples_color, const RenderImage& stencil,
                          const RenderImage& mask, VkExtent2D extent, VkBuffer vertices, std::uint32_t first_vertex,
                          std::uint32_t vertex_count, bool even_odd) {
    image_barrier(cmd, samples_color.image(), VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                  VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                  VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                  VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    image_barrier(cmd, mask.image(), VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                  VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                  VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                  VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    image_barrier(cmd, stencil.image(), stencil_aspect_, VK_IMAGE_LAYOUT_UNDEFINED,
                  VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                  VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                  VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                  VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                  VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);

    VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    color.imageView = samples_color.view();
    color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
    color.resolveImageView = mask.view();
    color.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkRenderingAttachmentInfo depth_stencil{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    depth_stencil.imageView = stencil.view();
    depth_stencil.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depth_stencil.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth_stencil.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
    rendering.renderArea = {{0, 0}, extent};
    rendering.layerCount = 1;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachments = &color;
    rendering.pDepthAttachment = has_depth(stencil_format_) ? &depth_stencil : nullptr;
    rendering.pStencilAttachment = &depth_stencil;
    vkCmdBeginRendering(cmd, &rendering);

    const VkViewport view{0, 0, static_cast<float>(extent.width), static_cast<float>(extent.height), 0, 1};
    const VkRect2D scissor{{0, 0}, extent};
    vkCmdSetViewport(cmd, 0, 1, &view);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    const PushConstants push{{static_cast<float>(extent.width), static_cast<float>(extent.height)}};

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, even_odd ? stencil_even_odd_ : stencil_non_zero_);
    vkCmdPushConstants(cmd, layout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);
    const VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &vertices, &offset);
    vkCmdDraw(cmd, vertex_count, 1, first_vertex, 0);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, even_odd ? cover_even_odd_ : cover_non_zero_);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdEndRendering(cmd);

    image_barrier(cmd, mask.image(), VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                  VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                  VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
}

} // namespace elg::detail::gpu
