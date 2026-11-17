// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_GPU_BUFFER_H
#define ELG_GPU_BUFFER_H

#include "gpu/webgpu_types.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace elg::detail::gpu {

// A GPU buffer for per-frame data that grows on demand. Writes go through the queue, so they
// land after earlier submissions finish reading and before later ones start: one buffer serves
// every frame.
class Buffer {
public:
    Buffer(const wgpu::Device& device, wgpu::BufferUsage usage);
    ~Buffer();

    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;
    Buffer(Buffer&&) = delete;
    Buffer& operator=(Buffer&&) = delete;

    // Replaces the contents, growing the buffer if needed. Always leaves a (non-empty) buffer to
    // bind, even for empty data.
    void write(std::span<const std::byte> data);

    [[nodiscard]] const wgpu::Buffer& handle() const noexcept;

private:
    struct Objects; // the device and the buffer
    std::unique_ptr<Objects> objects_;
    wgpu::BufferUsage usage_;
    std::uint64_t capacity_ = 0;
};

} // namespace elg::detail::gpu

#endif // ELG_GPU_BUFFER_H
