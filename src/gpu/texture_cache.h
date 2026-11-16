// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_GPU_TEXTURE_CACHE_H
#define ELG_GPU_TEXTURE_CACHE_H

#include "gpu/eviction.h"
#include "gpu/webgpu_types.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace elg::detail::gpu {

class Device;

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

// An image's pixels as TextureCache uploads them: RGBA8, sRGB, straight alpha, rows top to
// bottom. `id` names the image, the same for every frame that draws it (a decoded image's).
struct ImagePixels {
    std::uint64_t id = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::span<const std::uint8_t> rgba;
};

// Images' GPU textures (design.md §13.3), shared by every window: sRGB RGBA with a full mip
// chain, so photos drawn small stay smooth. A texture is uploaded the first time a frame draws
// its image (its mips rendered in that frame's commands), and released as kDecodedImagePolicy
// says once frames stop drawing it. Render thread only.
class TextureCache {
public:
    // `policy`: kDecodedImagePolicy, unless tests ask for another.
    TextureCache(const Device& device, wgpu::BindGroupLayout image_layout,
                 CachePolicy policy = kDecodedImagePolicy);
    ~TextureCache();

    TextureCache(const TextureCache&) = delete;
    TextureCache& operator=(const TextureCache&) = delete;
    TextureCache(TextureCache&&) = delete;
    TextureCache& operator=(TextureCache&&) = delete;

    // Starts a window's frame, drawn at `now`: the textures it acquires are the ones it needs.
    void begin_frame(CacheClock::time_point now) noexcept { frame_.begin(now); }

    // The bind group (texture and sampler) for `image`, uploading it and recording its mips into
    // `encoder` if it isn't on the GPU yet. Null for an image with a side longer than the device's
    // textures can have: it gets no texture.
    wgpu::BindGroup acquire(const wgpu::CommandEncoder& encoder, const ImagePixels& image);

    // Ends the frame: releases textures as kDecodedImagePolicy says. (WebGPU keeps a texture
    // alive for frames still using it, so releasing one is always safe.)
    void end_frame();

    // How many images have a texture.
    [[nodiscard]] std::size_t size() const noexcept;

private:
    // The image layout, the sampler, the mip pipeline, and the textures by ImagePixels::id.
    struct Objects;

    void generate_mips(const wgpu::CommandEncoder& encoder, const wgpu::Texture& texture, std::uint32_t levels);

    const Device& device_;
    std::unique_ptr<Objects> objects_;
    CachePolicy policy_;
    CacheFrame frame_;
    bool warned_ = false; // about an image too large for a texture
};

} // namespace elg::detail::gpu

#endif // ELG_GPU_TEXTURE_CACHE_H
