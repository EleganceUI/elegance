// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_GPU_INSTANCE_H
#define ELG_GPU_INSTANCE_H

#include "gpu/vk.h"

#include <span>

namespace elg::detail::gpu {

// The Vulkan instance, plus the validation layer and its message callback when enabled.
// Validation is on by default in debug builds; ELG_VULKAN_VALIDATION=0/1 overrides it.
class Instance {
public:
    explicit Instance(std::span<const char* const> required_extensions);
    ~Instance();

    Instance(const Instance&) = delete;
    Instance& operator=(const Instance&) = delete;

    VkInstance handle() const noexcept { return instance_; }
    bool validation_enabled() const noexcept { return messenger_ != VK_NULL_HANDLE; }

private:
    VkInstance instance_ = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT messenger_ = VK_NULL_HANDLE;
};

// Owns a VkSurfaceKHR created by the platform layer. Surfaces are created and destroyed on the
// UI thread (SDL requires it); the render thread only borrows them, and is done with a surface
// before it's destroyed (design.md §15).
class Surface {
public:
    Surface(const Instance& instance, VkSurfaceKHR surface) noexcept;
    ~Surface();

    Surface(const Surface&) = delete;
    Surface& operator=(const Surface&) = delete;

    VkSurfaceKHR handle() const noexcept { return surface_; }

private:
    VkInstance instance_;
    VkSurfaceKHR surface_;
};

} // namespace elg::detail::gpu

#endif // ELG_GPU_INSTANCE_H
