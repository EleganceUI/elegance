// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_GPU_VIDEO_TEXTURES_H
#define ELG_GPU_VIDEO_TEXTURES_H

#include "gpu/buffer.h"
#include "gpu/device.h"
#include "gpu/vk.h"
#include "media/video_source.h"

#include <array>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace elg::detail::gpu {

// Video frames' textures (design.md §13.3). Render thread only.
// - Shared textures (VideoFrame::Kind::d3d11_shared): imported into Vulkan once each and sampled
//   in place. The decoder writes them outside Vulkan, so every command buffer that samples one
//   acquires it from the external queue family first and releases it at the end.
// - Frames in memory (Kind::cpu): written to a storage buffer and turned into a BGRA image by a
//   Vulkan pass (shaders/video_convert.frag) that also converts YUV, so every platform converts
//   the same way. A ring of three images per source keeps frames in flight intact.
class VideoTextures {
public:
    VideoTextures(const Device& device, VkDescriptorSetLayout image_layout);
    ~VideoTextures();

    VideoTextures(const VideoTextures&) = delete;
    VideoTextures& operator=(const VideoTextures&) = delete;

    // The descriptor set to sample `frame` with: importing its texture the first time and
    // recording the acquire into `cmd` (once per command buffer: `acquired` lists what this one
    // has acquired), or for frames in memory, recording the upload and conversion into `cmd`
    // (outside any rendering). VK_NULL_HANDLE if it can't be shown.
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

    // Frames in memory: per source, three converted images and their upload buffers.
    struct CpuSlot {
        VkImage image = VK_NULL_HANDLE;
        VmaAllocation allocation = VK_NULL_HANDLE;
        VkImageView render_view = VK_NULL_HANDLE; // UNORM: the pass writes gamma-encoded values
        VkImageView sample_view = VK_NULL_HANDLE; // sRGB: sampling linearizes them
        VkDescriptorSet sample_set = VK_NULL_HANDLE;
        VkDescriptorSet frame_set = VK_NULL_HANDLE; // the storage buffer, for the pass
        std::unique_ptr<Buffer> pixels;
        VkBuffer bound = VK_NULL_HANDLE; // the buffer frame_set points at
        bool used = false;
    };
    struct CpuRing {
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::array<CpuSlot, 3> slots;
        int current = -1;
        std::uint64_t frame_id = 0;
        std::uint64_t last_used = 0;
    };
    VkDescriptorSet convert(VkCommandBuffer cmd, const VideoFrame& frame);
    bool create_slot(CpuSlot& slot, std::uint32_t width, std::uint32_t height);
    void destroy(CpuSlot& slot) noexcept;
    void create_converter();

    const Device& device_;
    VkDescriptorSetLayout layout_;
    VkSampler sampler_ = VK_NULL_HANDLE;
    VkDescriptorPool pool_ = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> free_sets_;
    std::unordered_map<std::uint64_t, Texture> textures_; // by VideoFrame::texture_id
    std::unordered_map<std::uint64_t, CpuRing> rings_;    // by VideoFrame::source_id
    VkDescriptorSetLayout frame_layout_ = VK_NULL_HANDLE;  // one storage buffer
    VkDescriptorPool frame_pool_ = VK_NULL_HANDLE;
    VkPipelineLayout convert_layout_ = VK_NULL_HANDLE;
    VkPipeline convert_pipeline_ = VK_NULL_HANDLE;
    VkShaderModule convert_vertex_ = VK_NULL_HANDLE;
    VkShaderModule convert_fragment_ = VK_NULL_HANDLE;
    std::uint64_t frame_ = 0;
    void* get_handle_properties_ = nullptr; // PFN_vkGetMemoryWin32HandlePropertiesKHR
};

} // namespace elg::detail::gpu

#endif // ELG_GPU_VIDEO_TEXTURES_H
