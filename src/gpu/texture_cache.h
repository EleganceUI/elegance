// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_GPU_TEXTURE_CACHE_H
#define ELG_GPU_TEXTURE_CACHE_H

#include "gpu/device.h"
#include "gpu/eviction.h"
#include "gpu/webgpu.h"
#include "media/image_decode.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <unordered_map>

namespace elg::detail::gpu {

// What TextureCache keeps of the textures that frames don't draw.
inline constexpr CachePolicy kDecodedImagePolicy{std::chrono::seconds(10), std::size_t{512} << 20};

// The GPU memory of a decoded image's texture: RGBA8, with every mip level.
constexpr std::size_t decoded_texture_bytes(std::uint32_t width, std::uint32_t height) {
    std::size_t bytes = 0;
    for (;;) {
        bytes += std::size_t{width} * height * 4;
        if (width <= 1 && height <= 1) {
            return bytes;
        }
        width = width > 1 ? width / 2 : 1;
        height = height > 1 ? height / 2 : 1;
    }
}

// Images' GPU textures (design.md §13.3), shared by every window: sRGB RGBA with a full mip
// chain, so photos drawn small stay smooth. A texture is uploaded the first time a frame draws
// its image (its mips rendered in that frame's commands), and released as kDecodedImagePolicy
// says once frames stop drawing it. Render thread only.
class TextureCache {
public:
    TextureCache(const Device& device, wgpu::BindGroupLayout  image_layout);

    TextureCache(const TextureCache&) = delete;
    TextureCache& operator=(const TextureCache&) = delete;
    TextureCache(TextureCache&&) = delete;
    TextureCache& operator=(TextureCache&&) = delete;
    ~TextureCache() = default;

    // Starts a window's frame, drawn at `now`: the textures it acquires are the ones it needs.
    void begin_frame(CacheClock::time_point now) noexcept { frame_.begin(now); }

    // The bind group (texture and sampler) for `image`, uploading it and recording its mips into
    // `encoder` if it isn't on the GPU yet.
    wgpu::BindGroup acquire(const wgpu::CommandEncoder& encoder, const DecodedImage& image);

    // Ends the frame: releases textures as kDecodedImagePolicy says. (WebGPU keeps a texture
    // alive for frames still using it, so releasing one is always safe.)
    void end_frame();

private:
    struct Texture {
        wgpu::Texture texture;
        wgpu::BindGroup group;
        CacheUse use;
    };

    void generate_mips(const wgpu::CommandEncoder& encoder, const wgpu::Texture& texture, std::uint32_t levels);

    const Device& device_;
    wgpu::BindGroupLayout layout_;
    wgpu::Sampler sampler_;
    wgpu::RenderPipeline mip_pipeline_;
    std::unordered_map<std::uint64_t, Texture> textures_; // by DecodedImage::id
    CacheFrame frame_;
};

} // namespace elg::detail::gpu

#endif // ELG_GPU_TEXTURE_CACHE_H
