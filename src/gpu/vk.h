// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_GPU_VK_H
#define ELG_GPU_VK_H

// Single include point for Vulkan: volk loads every function at runtime, and VMA takes its
// function pointers from volk (vmaImportVulkanFunctionsFromVolk).

#include <volk.h>

#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 0
#include <vma/vk_mem_alloc.h>

#include <string>
#include <string_view>

namespace elg::detail::gpu {

std::string to_string(VkResult result);

// Throws elg::GpuError describing `what` failed.
[[noreturn]] void throw_vk_error(VkResult result, std::string_view what);

// Throws elg::GpuError for error results; success codes (including VK_SUBOPTIMAL_KHR) pass.
inline void check(VkResult result, std::string_view what) {
    if (result < 0) [[unlikely]] {
        throw_vk_error(result, what);
    }
}

} // namespace elg::detail::gpu

#endif // ELG_GPU_VK_H
