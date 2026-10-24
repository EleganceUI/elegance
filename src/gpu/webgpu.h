// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_GPU_WEBGPU_H
#define ELG_GPU_WEBGPU_H

// Single include point for WebGPU (Dawn, design.md §5). Dawn runs it on Direct3D 12 or 11,
// Vulkan, or Metal, whichever the platform has.

#include <webgpu/webgpu_cpp.h>

#include <cstdint>
#include <string>
#include <string_view>

namespace elg::detail::gpu {

std::string to_string(wgpu::StringView text);
std::string_view to_string(wgpu::BackendType backend);

// Throws elg::GpuError describing `what` failed.
[[noreturn]] void throw_gpu_error(std::string_view what, std::string_view detail = {});

// A shader module from WGSL source (shaders/*.wgsl, embedded by elg_add_shaders()).
wgpu::ShaderModule create_shader(const wgpu::Device& device, const char* wgsl, std::string_view label);

// The two-entry bind group layout every sampled texture uses: binding 0 a filterable 2D texture,
// binding 1 a filtering sampler (fragment stage).
wgpu::BindGroupLayout create_image_layout(const wgpu::Device& device);
wgpu::BindGroup create_image_group(const wgpu::Device& device, const wgpu::BindGroupLayout& layout,
                                   const wgpu::TextureView& view, const wgpu::Sampler& sampler);
// Linear filtering, clamped to the edges; mipmaps interpolated where the texture has them.
wgpu::Sampler create_linear_sampler(const wgpu::Device& device);

// The 8-bit sRGB format with the same layout as `format` (BGRA8Unorm -> BGRA8UnormSrgb), or
// `format` itself.
wgpu::TextureFormat srgb_variant(wgpu::TextureFormat format);

} // namespace elg::detail::gpu

#endif // ELG_GPU_WEBGPU_H
