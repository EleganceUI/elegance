// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_GPU_BUFFER_H
#define ELG_GPU_BUFFER_H

#include "gpu/vk.h"

#include <cstddef>
#include <span>

namespace elg::detail::gpu {

// A persistently mapped, CPU-written buffer that grows on demand. Used for per-frame data;
// callers must not write while the GPU may still read it (one buffer per frame in flight).
class Buffer {
public:
    Buffer(VmaAllocator allocator, VkBufferUsageFlags usage) noexcept;
    ~Buffer();

    Buffer(Buffer&& other) noexcept;
    Buffer& operator=(Buffer&& other) noexcept;
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;

    // Replaces the contents, growing the buffer if needed.
    void write(std::span<const std::byte> data);

    VkBuffer handle() const noexcept { return buffer_; }

private:
    void release() noexcept;

    VmaAllocator allocator_ = VK_NULL_HANDLE;
    VkBufferUsageFlags usage_ = 0;
    VkBuffer buffer_ = VK_NULL_HANDLE;
    VmaAllocation allocation_ = VK_NULL_HANDLE;
    void* mapped_ = nullptr;
    VkDeviceSize capacity_ = 0;
};

} // namespace elg::detail::gpu

#endif // ELG_GPU_BUFFER_H
