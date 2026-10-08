// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include "gpu/quad_renderer.h"

#include <cstddef>

namespace elg::detail::gpu {

namespace {

// SPIR-V generated at build time by elg_add_shaders() (cmake/ElgShaders.cmake).
constexpr std::uint32_t kUiVertSpirv[] = {
#include "shaders/ui.vert.inc"
};
constexpr std::uint32_t kUiFragSpirv[] = {
#include "shaders/ui.frag.inc"
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

} // namespace

QuadRenderer::QuadRenderer(const Device& device, VkDescriptorSetLayout image_layout) : device_(device) {
    try {
        vertex_ = create_module(device_.handle(), kUiVertSpirv, sizeof(kUiVertSpirv));
        fragment_ = create_module(device_.handle(), kUiFragSpirv, sizeof(kUiFragSpirv));

        VkDescriptorSetLayoutBinding stops_binding{};
        stops_binding.binding = 0;
        stops_binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        stops_binding.descriptorCount = 1;
        stops_binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorSetLayoutCreateInfo stops_info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        stops_info.bindingCount = 1;
        stops_info.pBindings = &stops_binding;
        check(vkCreateDescriptorSetLayout(device_.handle(), &stops_info, nullptr, &stops_layout_),
              "create the gradient stops layout");

        const VkDescriptorSetLayout set_layouts[] = {image_layout, stops_layout_};
        VkPushConstantRange push{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstants)};
        VkPipelineLayoutCreateInfo layout_info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        layout_info.setLayoutCount = 2;
        layout_info.pSetLayouts = set_layouts;
        layout_info.pushConstantRangeCount = 1;
        layout_info.pPushConstantRanges = &push;
        check(vkCreatePipelineLayout(device_.handle(), &layout_info, nullptr, &layout_),
              "create the UI pipeline layout");
    } catch (...) {
        destroy();
        throw;
    }
}

QuadRenderer::~QuadRenderer() {
    destroy();
}

void QuadRenderer::destroy() noexcept {
    const VkDevice device = device_.handle();
    for (auto& [format, pipeline] : pipelines_) {
        vkDestroyPipeline(device, pipeline, nullptr);
    }
    pipelines_.clear();
    vkDestroyPipelineLayout(device, layout_, nullptr);
    vkDestroyDescriptorSetLayout(device, stops_layout_, nullptr);
    vkDestroyShaderModule(device, fragment_, nullptr);
    vkDestroyShaderModule(device, vertex_, nullptr);
    layout_ = VK_NULL_HANDLE;
    stops_layout_ = VK_NULL_HANDLE;
    fragment_ = VK_NULL_HANDLE;
    vertex_ = VK_NULL_HANDLE;
}

void QuadRenderer::begin(VkCommandBuffer cmd, VkFormat color_format, VkExtent2D extent, VkBuffer instances,
                         VkDescriptorSet image, VkDescriptorSet stops) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_for(color_format));
    const VkDescriptorSet sets[] = {image, stops};
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 0, 2, sets, 0, nullptr);

    const PushConstants push{{static_cast<float>(extent.width), static_cast<float>(extent.height)}};
    vkCmdPushConstants(cmd, layout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);

    const VkViewport viewport{0, 0, push.viewport[0], push.viewport[1], 0, 1};
    const VkRect2D scissor{{0, 0}, extent};
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    if (instances) {
        const VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &instances, &offset);
    }
}

void QuadRenderer::bind_image(VkCommandBuffer cmd, VkDescriptorSet image) {
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 0, 1, &image, 0, nullptr);
}

void QuadRenderer::draw(VkCommandBuffer cmd, std::uint32_t first_instance, std::uint32_t count) {
    if (count > 0) {
        vkCmdDraw(cmd, 4, count, 0, first_instance);
    }
}

VkPipeline QuadRenderer::pipeline_for(VkFormat format) {
    for (const auto& [cached_format, pipeline] : pipelines_) {
        if (cached_format == format) {
            return pipeline;
        }
    }

    const VkPipelineShaderStageCreateInfo stages[] = {
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT,
         vertex_, "main", nullptr},
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
         VK_SHADER_STAGE_FRAGMENT_BIT, fragment_, "main", nullptr},
    };

    const VkVertexInputBindingDescription binding{0, sizeof(QuadInstance), VK_VERTEX_INPUT_RATE_INSTANCE};
    const VkVertexInputAttributeDescription attributes[] = {
        {0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(QuadInstance, rect)},
        {1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(QuadInstance, shape)},
        {2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(QuadInstance, color)},
        {3, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(QuadInstance, border_color)},
        {4, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(QuadInstance, params)},
        {5, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(QuadInstance, gradient)},
        {6, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(QuadInstance, clip_rect)},
        {7, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(QuadInstance, clip_radii)},
        {8, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(QuadInstance, extra)},
    };
    VkPipelineVertexInputStateCreateInfo vertex_input{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertex_input.vertexBindingDescriptionCount = 1;
    vertex_input.pVertexBindingDescriptions = &binding;
    vertex_input.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(std::size(attributes));
    vertex_input.pVertexAttributeDescriptions = attributes;

    VkPipelineInputAssemblyStateCreateInfo input_assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;

    VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Premultiplied alpha "over".
    VkPipelineColorBlendAttachmentState blend{};
    blend.blendEnable = VK_TRUE;
    blend.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend.colorBlendOp = VK_BLEND_OP_ADD;
    blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend.alphaBlendOp = VK_BLEND_OP_ADD;
    blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                           VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo color_blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    color_blend.attachmentCount = 1;
    color_blend.pAttachments = &blend;

    const VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount = static_cast<std::uint32_t>(std::size(dynamic_states));
    dynamic.pDynamicStates = dynamic_states;

    VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachmentFormats = &format;

    VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    info.pNext = &rendering;
    info.stageCount = static_cast<std::uint32_t>(std::size(stages));
    info.pStages = stages;
    info.pVertexInputState = &vertex_input;
    info.pInputAssemblyState = &input_assembly;
    info.pViewportState = &viewport;
    info.pRasterizationState = &raster;
    info.pMultisampleState = &multisample;
    info.pColorBlendState = &color_blend;
    info.pDynamicState = &dynamic;
    info.layout = layout_;

    VkPipeline pipeline = VK_NULL_HANDLE;
    check(vkCreateGraphicsPipelines(device_.handle(), VK_NULL_HANDLE, 1, &info, nullptr, &pipeline),
          "create the UI pipeline");
    pipelines_.emplace_back(format, pipeline);
    return pipeline;
}

} // namespace elg::detail::gpu
