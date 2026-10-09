// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_GPU_VIDEO_TEXTURES_H
#define ELG_GPU_VIDEO_TEXTURES_H

#include "gpu/device.h"
#include "gpu/vk.h"
#include "media/video_source.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace elg::detail::gpu {

// Video frames' textures (design.md §13.3): the decoder's shared textures, imported into Vulkan
// once each and sampled in place (no copy). The decoder writes them outside Vulkan, so every
// command buffer that samples one acquires it from the external queue family first and releases
// it at the end. Render thread only.
class VideoTextures {
public:
    VideoTextures(const Device& device, VkDescriptorSetLayout image_layout);
    ~VideoTextures();

    VideoTextures(const VideoTextures&) = delete;
    VideoTextures& operator=(const VideoTextures&) = delete;

    // The descriptor set for `frame`, importing its texture the first time. Records the acquire
    // into `cmd` (once per command buffer: `acquired` lists what this one has acquired).
    // Returns VK_NULL_HANDLE if the texture can't be imported.
    VkDescriptorSet acquire(VkCommandBuffer cmd, const VideoFrame& frame, std::vector<VkImage>& acquired);
    // Records the release of `acquired` back to the decoder, at the end of the command buffer.
    void release(VkCommandBuffer cmd, const std::vector<VkImage>& acquired) const;

    // Once per rendered frame: drops textures no frame has used for a while (a video that
    // changed size, or stopped being shown).
    void end_frame() noexcept;

    static bool supported(const Device& device);

private:
    struct Texture {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkDescriptorSet set = VK_NULL_HANDLE;
        std::uint64_t last_used = 0;
        bool used = false; // its first acquire comes from UNDEFINED (Vulkan's view of a new image)
    };

    bool import(const VideoFrame& frame, Texture& texture);
    void destroy(Texture& texture) noexcept;

    const Device& device_;
    VkDescriptorSetLayout layout_;
    VkSampler sampler_ = VK_NULL_HANDLE;
    VkDescriptorPool pool_ = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> free_sets_;
    std::unordered_map<std::uint64_t, Texture> textures_; // by VideoFrame::texture_id
    std::uint64_t frame_ = 0;
    void* get_handle_properties_ = nullptr; // PFN_vkGetMemoryWin32HandlePropertiesKHR
};

} // namespace elg::detail::gpu

#endif // ELG_GPU_VIDEO_TEXTURES_H
