// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_GPU_TEXTURE_CACHE_H
#define ELG_GPU_TEXTURE_CACHE_H

#include "gpu/buffer.h"
#include "gpu/device.h"
#include "gpu/vk.h"
#include "media/image_decode.h"

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace elg::detail::gpu {

// Images' GPU textures (design.md §13.3), shared by every window: sRGB RGBA with a full mip
// chain, so photos drawn small stay smooth. A texture is uploaded the first time a frame draws
// its image, inside that frame's command buffer, and dropped after it goes unused for a while.
// Render thread only.
class TextureCache {
public:
    TextureCache(const Device& device, VkDescriptorSetLayout image_layout);
    ~TextureCache();

    TextureCache(const TextureCache&) = delete;
    TextureCache& operator=(const TextureCache&) = delete;

    // The descriptor set (one combined image sampler) for `image`, recording its upload into
    // `cmd` if it isn't on the GPU yet. The upload's staging buffer goes into `keep_alive`, which
    // the caller holds until `cmd` has finished.
    VkDescriptorSet acquire(VkCommandBuffer cmd, const DecodedImage& image, std::vector<Buffer>& keep_alive);

    // Call once per rendered frame: drops textures unused for a while, and over the memory
    // budget, the least recently used ones.
    void end_frame() noexcept;

private:
    struct Texture {
        VkImage image = VK_NULL_HANDLE;
        VmaAllocation allocation = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkDescriptorSet set = VK_NULL_HANDLE;
        std::size_t bytes = 0;
        std::uint64_t last_used = 0;
    };

    void destroy(Texture& texture) noexcept;
    VkDescriptorSet allocate_set();

    const Device& device_;
    VkDescriptorSetLayout layout_;
    VkSampler sampler_ = VK_NULL_HANDLE;
    std::vector<VkDescriptorPool> pools_;
    std::vector<VkDescriptorSet> free_sets_;
    std::unordered_map<std::uint64_t, Texture> textures_; // by DecodedImage::id
    std::size_t bytes_ = 0;
    std::uint64_t frame_ = 0;
};

} // namespace elg::detail::gpu

#endif // ELG_GPU_TEXTURE_CACHE_H
