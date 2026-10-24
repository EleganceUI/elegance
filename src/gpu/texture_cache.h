// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_GPU_TEXTURE_CACHE_H
#define ELG_GPU_TEXTURE_CACHE_H

#include "gpu/device.h"
#include "gpu/webgpu.h"
#include "media/image_decode.h"

#include <cstdint>
#include <unordered_map>

namespace elg::detail::gpu {

// Images' GPU textures (design.md §13.3), shared by every window: sRGB RGBA with a full mip
// chain, so photos drawn small stay smooth. A texture is uploaded the first time a frame draws
// its image (its mips rendered in that frame's commands), and dropped after it goes unused for
// a while. Render thread only.
class TextureCache {
public:
    TextureCache(const Device& device, const wgpu::BindGroupLayout& image_layout);

    TextureCache(const TextureCache&) = delete;
    TextureCache& operator=(const TextureCache&) = delete;

    // The bind group (texture and sampler) for `image`, uploading it and recording its mips into
    // `encoder` if it isn't on the GPU yet.
    wgpu::BindGroup acquire(const wgpu::CommandEncoder& encoder, const DecodedImage& image);

    // Call once per rendered frame: drops textures unused for a while, and over the memory
    // budget, the least recently used ones. (WebGPU keeps a texture alive for frames still
    // using it, so dropping one is always safe.)
    void end_frame() noexcept;

private:
    struct Texture {
        wgpu::Texture texture;
        wgpu::BindGroup group;
        std::size_t bytes = 0;
        std::uint64_t last_used = 0;
    };

    void generate_mips(const wgpu::CommandEncoder& encoder, const wgpu::Texture& texture, std::uint32_t levels);
    void destroy(Texture& texture) noexcept;

    const Device& device_;
    wgpu::BindGroupLayout layout_;
    wgpu::Sampler sampler_;
    wgpu::RenderPipeline mip_pipeline_;
    std::unordered_map<std::uint64_t, Texture> textures_; // by DecodedImage::id
    std::size_t bytes_ = 0;
    std::uint64_t frame_ = 0;
};

} // namespace elg::detail::gpu

#endif // ELG_GPU_TEXTURE_CACHE_H
