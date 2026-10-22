// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include "gpu/device.h"

#include "core/log.h"

#include <elg/error.h>

#include <cstring>
#include <format>
#include <optional>
#include <vector>

namespace elg::detail::gpu {

namespace {

constexpr const char* kPortabilitySubset = "VK_KHR_portability_subset";

struct Candidate {
    VkPhysicalDevice device = VK_NULL_HANDLE;
    std::uint32_t queue_family = 0;
    bool portability_subset = false;
    bool external_memory_win32 = false;
    int score = 0;
    std::string name;
};

bool has_extension(const std::vector<VkExtensionProperties>& extensions, const char* name) {
    for (const VkExtensionProperties& extension : extensions) {
        if (std::strcmp(extension.extensionName, name) == 0) {
            return true;
        }
    }
    return false;
}

// Returns the candidate, or nullopt with `why` explaining the rejection.
std::optional<Candidate> evaluate(VkPhysicalDevice device, VkSurfaceKHR surface, std::string& why) {
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(device, &props);
    Candidate candidate{device};
    candidate.name = props.deviceName;

    if (props.apiVersion < VK_API_VERSION_1_3) {
        why = "supports only Vulkan " + std::to_string(VK_API_VERSION_MAJOR(props.apiVersion)) + "." +
              std::to_string(VK_API_VERSION_MINOR(props.apiVersion));
        return std::nullopt;
    }

    VkPhysicalDeviceVulkan13Features features13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &features13};
    vkGetPhysicalDeviceFeatures2(device, &features);
    if (!features13.dynamicRendering || !features13.synchronization2) {
        why = "lacks dynamic rendering or synchronization2";
        return std::nullopt;
    }

    std::uint32_t extension_count = 0;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extension_count, nullptr);
    std::vector<VkExtensionProperties> extensions(extension_count);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extension_count, extensions.data());
    if (!has_extension(extensions, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) {
        why = "has no swapchain support";
        return std::nullopt;
    }
    candidate.portability_subset = has_extension(extensions, kPortabilitySubset);
    candidate.external_memory_win32 = has_extension(extensions, "VK_KHR_external_memory_win32");

    std::uint32_t family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &family_count, nullptr);
    std::vector<VkQueueFamilyProperties> families(family_count);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &family_count, families.data());
    bool found = false;
    for (std::uint32_t i = 0; i < family_count && !found; ++i) {
        VkBool32 present = VK_FALSE;
        if (vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &present) != VK_SUCCESS) {
            present = VK_FALSE; // the output is undefined on failure
        }
        if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present) {
            candidate.queue_family = i;
            found = true;
        }
    }
    if (!found) {
        why = "has no queue that can both render and present to the window";
        return std::nullopt;
    }

    switch (props.deviceType) {
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: candidate.score = 1000; break;
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: candidate.score = 100; break;
    default: candidate.score = 10; break;
    }
    return candidate;
}

} // namespace

Device::Device(const Instance& instance, VkSurfaceKHR surface) {
    std::uint32_t count = 0;
    vkEnumeratePhysicalDevices(instance.handle(), &count, nullptr);
    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(instance.handle(), &count, devices.data());
    if (devices.empty()) {
        throw GpuError("no Vulkan-capable GPU was found");
    }

    std::optional<Candidate> best;
    std::string rejections;
    for (VkPhysicalDevice device : devices) {
        std::string why;
        if (auto candidate = evaluate(device, surface, why)) {
            if (!best || candidate->score > best->score) {
                best = std::move(candidate);
            }
        } else {
            VkPhysicalDeviceProperties props;
            vkGetPhysicalDeviceProperties(device, &props);
            rejections += std::format("\n  {}: {}", props.deviceName, why);
        }
    }
    if (!best) {
        throw GpuError("no GPU meets Elegence's requirements (Vulkan 1.3 with dynamic rendering "
                       "and presentation):" + rejections);
    }

    physical_ = best->device;
    queue_family_ = best->queue_family;
    name_ = best->name;

    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queue_info{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queue_info.queueFamilyIndex = queue_family_;
    queue_info.queueCount = 1;
    queue_info.pQueuePriorities = &priority;

    VkPhysicalDeviceVulkan13Features features13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    features13.dynamicRendering = VK_TRUE;
    features13.synchronization2 = VK_TRUE;

    std::vector<const char*> extensions{VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    if (best->portability_subset) {
        extensions.push_back(kPortabilitySubset);
    }
#if defined(_WIN32)
    // Transparent windows share images with Direct3D (gpu/composition_win32.h).
    if (best->external_memory_win32) {
        extensions.push_back("VK_KHR_external_memory_win32");
    }
#endif
    for (const char* extension : extensions) {
        extensions_.emplace_back(extension);
    }

    VkDeviceCreateInfo info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    info.pNext = &features13;
    info.queueCreateInfoCount = 1;
    info.pQueueCreateInfos = &queue_info;
    info.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size());
    info.ppEnabledExtensionNames = extensions.data();

    check(vkCreateDevice(physical_, &info, nullptr, &device_), "create the logical device");
    try {
        volkLoadDevice(device_);
        vkGetDeviceQueue(device_, queue_family_, 0, &queue_);

        VmaAllocatorCreateInfo allocator_info{};
        allocator_info.physicalDevice = physical_;
        allocator_info.device = device_;
        allocator_info.instance = instance.handle();
        allocator_info.vulkanApiVersion = VK_API_VERSION_1_3;
        VmaVulkanFunctions functions{};
        check(vmaImportVulkanFunctionsFromVolk(&allocator_info, &functions),
              "load functions for the memory allocator");
        allocator_info.pVulkanFunctions = &functions;
        check(vmaCreateAllocator(&allocator_info, &allocator_), "create the memory allocator");
    } catch (...) {
        destroy();
        throw;
    }

    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(physical_, &props);
    log_info("GPU: {} (Vulkan {}.{}.{})", name_, VK_API_VERSION_MAJOR(props.apiVersion),
             VK_API_VERSION_MINOR(props.apiVersion), VK_API_VERSION_PATCH(props.apiVersion));
}

Device::~Device() {
    destroy();
}

void Device::destroy() noexcept {
    if (device_) {
        vkDeviceWaitIdle(device_);
    }
    if (allocator_) {
        vmaDestroyAllocator(allocator_);
        allocator_ = VK_NULL_HANDLE;
    }
    if (device_) {
        vkDestroyDevice(device_, nullptr);
        device_ = VK_NULL_HANDLE;
    }
}

bool Device::has_extension(std::string_view name) const {
    return std::ranges::find(extensions_, name) != extensions_.end();
}

bool Device::can_present(VkSurfaceKHR surface) const {
    VkBool32 supported = VK_FALSE;
    check(vkGetPhysicalDeviceSurfaceSupportKHR(physical_, queue_family_, surface, &supported),
          "query presentation support");
    return supported == VK_TRUE;
}

void Device::wait_idle() const {
    check(vkDeviceWaitIdle(device_), "wait for the GPU to finish");
}

} // namespace elg::detail::gpu
