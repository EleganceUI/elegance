// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include "gpu/webgpu.h"

#include <elg/error.h>

#include <format>

namespace elg::detail::gpu {

std::string to_string(wgpu::StringView text) {
    return std::string(std::string_view(text));
}

std::string_view to_string(wgpu::BackendType backend) {
    switch (backend) {
    case wgpu::BackendType::Null: return "null";
    case wgpu::BackendType::WebGPU: return "WebGPU";
    case wgpu::BackendType::D3D11: return "Direct3D 11";
    case wgpu::BackendType::D3D12: return "Direct3D 12";
    case wgpu::BackendType::Metal: return "Metal";
    case wgpu::BackendType::Vulkan: return "Vulkan";
    case wgpu::BackendType::OpenGL: return "OpenGL";
    case wgpu::BackendType::OpenGLES: return "OpenGL ES";
    default: return "unknown";
    }
}

void throw_gpu_error(std::string_view what, std::string_view detail) {
    if (detail.empty()) {
        throw GpuError(std::format("WebGPU: failed to {}", what));
    }
    throw GpuError(std::format("WebGPU: failed to {}: {}", what, detail));
}

wgpu::ShaderModule create_shader(const wgpu::Device& device, const char* wgsl, std::string_view label) {
    wgpu::ShaderSourceWGSL source;
    source.code = wgsl;
    wgpu::ShaderModuleDescriptor descriptor;
    descriptor.nextInChain = &source;
    descriptor.label = label;
    // Compilation errors arrive through the device's error callback: shaders are ours, so in
    // debug builds that's an internal error naming the line.
    return device.CreateShaderModule(&descriptor);
}

wgpu::BindGroupLayout create_image_layout(const wgpu::Device& device) {
    wgpu::BindGroupLayoutEntry entries[2];
    entries[0].binding = 0;
    entries[0].visibility = wgpu::ShaderStage::Fragment;
    entries[0].texture.sampleType = wgpu::TextureSampleType::Float;
    entries[0].texture.viewDimension = wgpu::TextureViewDimension::e2D;
    entries[1].binding = 1;
    entries[1].visibility = wgpu::ShaderStage::Fragment;
    entries[1].sampler.type = wgpu::SamplerBindingType::Filtering;
    wgpu::BindGroupLayoutDescriptor descriptor;
    descriptor.label = "image";
    descriptor.entryCount = 2;
    descriptor.entries = entries;
    return device.CreateBindGroupLayout(&descriptor);
}

wgpu::BindGroup create_image_group(const wgpu::Device& device, const wgpu::BindGroupLayout& layout,
                                   const wgpu::TextureView& view, const wgpu::Sampler& sampler) {
    wgpu::BindGroupEntry entries[2];
    entries[0].binding = 0;
    entries[0].textureView = view;
    entries[1].binding = 1;
    entries[1].sampler = sampler;
    wgpu::BindGroupDescriptor descriptor;
    descriptor.layout = layout;
    descriptor.entryCount = 2;
    descriptor.entries = entries;
    return device.CreateBindGroup(&descriptor);
}

wgpu::Sampler create_linear_sampler(const wgpu::Device& device) {
    wgpu::SamplerDescriptor descriptor;
    descriptor.addressModeU = wgpu::AddressMode::ClampToEdge;
    descriptor.addressModeV = wgpu::AddressMode::ClampToEdge;
    descriptor.addressModeW = wgpu::AddressMode::ClampToEdge;
    descriptor.magFilter = wgpu::FilterMode::Linear;
    descriptor.minFilter = wgpu::FilterMode::Linear;
    descriptor.mipmapFilter = wgpu::MipmapFilterMode::Linear;
    return device.CreateSampler(&descriptor);
}

wgpu::TextureFormat srgb_variant(wgpu::TextureFormat format) {
    switch (format) {
    case wgpu::TextureFormat::BGRA8Unorm: return wgpu::TextureFormat::BGRA8UnormSrgb;
    case wgpu::TextureFormat::RGBA8Unorm: return wgpu::TextureFormat::RGBA8UnormSrgb;
    default: return format;
    }
}

} // namespace elg::detail::gpu
