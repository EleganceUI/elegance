// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_GPU_ATLAS_TEXTURE_H
#define ELG_GPU_ATLAS_TEXTURE_H

#include "gpu/buffer.h"
#include "gpu/device.h"
#include "gpu/vk.h"
#include "text/atlas.h"

#include <functional>
#include <span>

namespace elg::detail::gpu {

// The glyph atlas on the GPU: an RGBA8 MTSDF texture plus the descriptor set the UI shader
// samples it through. Filled from GlyphUploads produced on the UI thread.
class AtlasTexture {
public:
    explicit AtlasTexture(const Device& device);
    ~AtlasTexture();

    AtlasTexture(const AtlasTexture&) = delete;
    AtlasTexture& operator=(const AtlasTexture&) = delete;

    // Copies new glyphs into the texture. Blocks until done; glyphs are only uploaded the first
    // time they're seen, so this is rare.
    void upload(std::span<const GlyphUpload> uploads);

    VkDescriptorSetLayout set_layout() const noexcept { return set_layout_; }
    VkDescriptorSet descriptor_set() const noexcept { return descriptor_set_; }

private:
    void submit(const std::function<void(VkCommandBuffer)>& record);
    void destroy() noexcept;

    const Device& device_;
    VkImage image_ = VK_NULL_HANDLE;
    VmaAllocation allocation_ = VK_NULL_HANDLE;
    VkImageView view_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout set_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;
    VkDescriptorSet descriptor_set_ = VK_NULL_HANDLE;
    VkCommandPool command_pool_ = VK_NULL_HANDLE;
    VkCommandBuffer cmd_ = VK_NULL_HANDLE;
    VkFence fence_ = VK_NULL_HANDLE;
    Buffer staging_;
};

} // namespace elg::detail::gpu

#endif // ELG_GPU_ATLAS_TEXTURE_H
