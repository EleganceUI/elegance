// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_GPU_DEVICE_H
#define ELG_GPU_DEVICE_H

#include "gpu/instance.h"
#include "gpu/vk.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace elg::detail::gpu {

// The logical device, its graphics+present queue, and the memory allocator.
// Requires Vulkan 1.3 with dynamic rendering and synchronization2. Owned by the render thread.
class Device {
public:
    // `surface` is used to pick a queue family that can present to the app's windows.
    Device(const Instance& instance, VkSurfaceKHR surface);
    ~Device();

    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;

    VkDevice handle() const noexcept { return device_; }
    VkPhysicalDevice physical() const noexcept { return physical_; }
    VkQueue queue() const noexcept { return queue_; }
    std::uint32_t queue_family() const noexcept { return queue_family_; }
    VmaAllocator allocator() const noexcept { return allocator_; }
    const std::string& name() const noexcept { return name_; }

    bool can_present(VkSurfaceKHR surface) const;
    // Whether the device was created with this extension.
    bool has_extension(std::string_view name) const;
    void wait_idle() const;

private:
    void destroy() noexcept;

    VkPhysicalDevice physical_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    std::uint32_t queue_family_ = 0;
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    std::string name_;
    std::vector<std::string> extensions_;
};

} // namespace elg::detail::gpu

#endif // ELG_GPU_DEVICE_H
