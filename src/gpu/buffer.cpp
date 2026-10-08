// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include "gpu/buffer.h"

#include <algorithm>
#include <bit>
#include <cstring>
#include <utility>

namespace elg::detail::gpu {

namespace {

constexpr VkDeviceSize kMinCapacity = 64 * 1024;

}

Buffer::Buffer(VmaAllocator allocator, VkBufferUsageFlags usage) noexcept
    : allocator_(allocator), usage_(usage) {}

Buffer::~Buffer() {
    release();
}

Buffer::Buffer(Buffer&& other) noexcept
    : allocator_(other.allocator_), usage_(other.usage_),
      buffer_(std::exchange(other.buffer_, VK_NULL_HANDLE)),
      allocation_(std::exchange(other.allocation_, VK_NULL_HANDLE)),
      mapped_(std::exchange(other.mapped_, nullptr)), capacity_(std::exchange(other.capacity_, 0)) {}

Buffer& Buffer::operator=(Buffer&& other) noexcept {
    if (this != &other) {
        release();
        allocator_ = other.allocator_;
        usage_ = other.usage_;
        buffer_ = std::exchange(other.buffer_, VK_NULL_HANDLE);
        allocation_ = std::exchange(other.allocation_, VK_NULL_HANDLE);
        mapped_ = std::exchange(other.mapped_, nullptr);
        capacity_ = std::exchange(other.capacity_, 0);
    }
    return *this;
}

void Buffer::write(std::span<const std::byte> data) {
    if (data.empty()) {
        return;
    }
    if (data.size() > capacity_) {
        release();
        const VkDeviceSize capacity = std::max(kMinCapacity, std::bit_ceil(VkDeviceSize{data.size()}));

        VkBufferCreateInfo buffer_info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        buffer_info.size = capacity;
        buffer_info.usage = usage_;
        buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocation_info{};
        allocation_info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                                VMA_ALLOCATION_CREATE_MAPPED_BIT;
        allocation_info.usage = VMA_MEMORY_USAGE_AUTO;

        VmaAllocationInfo result{};
        check(vmaCreateBuffer(allocator_, &buffer_info, &allocation_info, &buffer_, &allocation_, &result),
              "allocate a GPU buffer");
        mapped_ = result.pMappedData;
        capacity_ = capacity;
    }
    std::memcpy(mapped_, data.data(), data.size());
    check(vmaFlushAllocation(allocator_, allocation_, 0, data.size()), "flush a GPU buffer");
}

void Buffer::release() noexcept {
    if (buffer_) {
        vmaDestroyBuffer(allocator_, buffer_, allocation_);
        buffer_ = VK_NULL_HANDLE;
        allocation_ = VK_NULL_HANDLE;
        mapped_ = nullptr;
        capacity_ = 0;
    }
}

} // namespace elg::detail::gpu
