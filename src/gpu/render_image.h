// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_GPU_RENDER_IMAGE_H
#define ELG_GPU_RENDER_IMAGE_H

// Offscreen images: rendered to and then sampled (layers, blur passes, path masks), or
// multisampled / stencil scratch attachments (path masks).

#include "gpu/webgpu_types.h"

#include <cstddef>
#include <cstdint>

namespace elg::detail::gpu {

// The texture of a `width` x `height` offscreen image of `format`, with `samples` per texel.
wgpu::Texture create_render_texture(const wgpu::Device& device, wgpu::TextureFormat format, std::uint32_t width,
                                    std::uint32_t height, std::uint32_t samples, wgpu::TextureUsage usage);

// The GPU memory an image takes: every sample of every texel.
std::size_t image_bytes(wgpu::TextureFormat format, std::uint32_t width, std::uint32_t height, std::uint32_t samples);

} // namespace elg::detail::gpu

#endif // ELG_GPU_RENDER_IMAGE_H
