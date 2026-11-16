// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include "gpu/buffer.h"

#include <webgpu/webgpu_cpp.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstring>

namespace elg::detail::gpu {

namespace {

constexpr std::uint64_t kMinCapacity = std::uint64_t{64} * 1024;

}

struct Buffer::Objects {
    wgpu::Device device;
    wgpu::Buffer buffer;
};

Buffer::Buffer(const wgpu::Device& device, wgpu::BufferUsage usage)
    : objects_(std::make_unique<Objects>(Objects{device, {}})), usage_(usage | wgpu::BufferUsage::CopyDst) {}

Buffer::~Buffer() = default;

const wgpu::Buffer& Buffer::handle() const noexcept {
    return objects_->buffer;
}

void Buffer::write(std::span<const std::byte> data) {
    wgpu::Buffer& buffer = objects_->buffer;
    const wgpu::Device& device = objects_->device;
    if (!buffer || data.size() > capacity_) {
        const std::uint64_t capacity = std::max(kMinCapacity, std::bit_ceil(std::uint64_t{data.size()}));
        wgpu::BufferDescriptor descriptor;
        descriptor.size = capacity;
        descriptor.usage = usage_;
        buffer = device.CreateBuffer(&descriptor);
        capacity_ = capacity;
    }
    // Queue writes come in multiples of 4 bytes: the last few go in a padded word.
    const std::size_t whole = data.size() & ~std::size_t{3};
    if (whole > 0) {
        device.GetQueue().WriteBuffer(buffer, 0, data.data(), whole);
    }
    if (whole < data.size()) {
        std::array<std::byte, 4> tail{};
        std::memcpy(tail.data(), data.data() + whole, data.size() - whole);
        device.GetQueue().WriteBuffer(buffer, whole, tail.data(), tail.size());
    }
}

} // namespace elg::detail::gpu
