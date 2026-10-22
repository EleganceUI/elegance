// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include "gpu/instance.h"

#include "core/crash.h"
#include "core/log.h"
#include "platform/platform.h"

#include <elg/error.h>

#include <algorithm>
#include <cstring>
#include <format>
#include <vector>

namespace elg::detail::gpu {

namespace {

constexpr const char* kValidationLayer = "VK_LAYER_KHRONOS_validation";

bool validation_requested() {
    if (auto value = platform::environment_variable("ELG_VULKAN_VALIDATION")) {
        return *value != "0";
    }
#ifdef NDEBUG
    return false;
#else
    return true;
#endif
}

bool has_layer(const char* name) {
    std::uint32_t count = 0;
    vkEnumerateInstanceLayerProperties(&count, nullptr);
    std::vector<VkLayerProperties> layers(count);
    vkEnumerateInstanceLayerProperties(&count, layers.data());
    return std::ranges::any_of(
        layers, [&](const VkLayerProperties& layer) { return std::strcmp(layer.layerName, name) == 0; });
}

bool has_instance_extension(const char* name) {
    std::uint32_t count = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> extensions(count);
    vkEnumerateInstanceExtensionProperties(nullptr, &count, extensions.data());
    return std::ranges::any_of(extensions, [&](const VkExtensionProperties& extension) {
        return std::strcmp(extension.extensionName, name) == 0;
    });
}

VKAPI_ATTR VkBool32 VKAPI_CALL on_validation_message(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT types,
    const VkDebugUtilsMessengerCallbackDataEXT* data, void*) {
    // A C callback: nothing may unwind through it.
    try {
        if (types == VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT) {
            // Loader chatter, mostly about third-party layers (overlays, recorders) we don't own.
            log_debug("vulkan: {}", data->pMessage);
        } else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
#ifdef NDEBUG
            log_error("vulkan: {}", data->pMessage);
#else
            // Every Vulkan object belongs to the framework, so a validation error is our bug.
            internal_error(std::format("Vulkan validation error: {}", data->pMessage));
#endif
        } else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
            log_warning("vulkan: {}", data->pMessage);
        }
    } catch (...) {
    }
    return VK_FALSE;
}

VkDebugUtilsMessengerCreateInfoEXT messenger_info() {
    VkDebugUtilsMessengerCreateInfoEXT info{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
    info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    info.pfnUserCallback = &on_validation_message;
    return info;
}

} // namespace

Instance::Instance(std::span<const char* const> required_extensions) {
    if (volkInitialize() != VK_SUCCESS) {
        throw GpuError("Vulkan is not available: the Vulkan loader could not be found");
    }
    const std::uint32_t loader_version = volkGetInstanceVersion();
    if (loader_version < VK_API_VERSION_1_3) {
        throw GpuError(std::format("Vulkan 1.3 is required, but the installed loader supports {}.{}",
                                   VK_API_VERSION_MAJOR(loader_version),
                                   VK_API_VERSION_MINOR(loader_version)));
    }

    // The window system's (VK_KHR_surface and the platform's surface extension): fail by name
    // rather than with vkCreateInstance's VK_ERROR_EXTENSION_NOT_PRESENT.
    for (const char* name : required_extensions) {
        if (!has_instance_extension(name)) {
            throw GpuError(std::format("Vulkan can't show windows: the instance extension {} is missing", name));
        }
    }
    std::vector<const char*> extensions(required_extensions.begin(), required_extensions.end());
    std::vector<const char*> layers;
    VkInstanceCreateFlags flags = 0;

    bool validation = false;
    if (validation_requested()) {
        if (has_layer(kValidationLayer) && has_instance_extension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME)) {
            layers.push_back(kValidationLayer);
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
            validation = true;
        } else {
            log_warning("Vulkan validation was requested, but {} is not installed", kValidationLayer);
        }
    }

    // MoltenVK (macOS) is a "portability" implementation and is only listed when asked for.
    if (has_instance_extension(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) {
        extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
        flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    }

    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "Elegence app";
    app.pEngineName = "Elegence";
    app.apiVersion = VK_API_VERSION_1_3;

    // Chaining the messenger info also reports problems during vkCreateInstance itself.
    const VkDebugUtilsMessengerCreateInfoEXT debug_info = messenger_info();

    VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    info.pNext = validation ? &debug_info : nullptr;
    info.flags = flags;
    info.pApplicationInfo = &app;
    info.enabledLayerCount = static_cast<std::uint32_t>(layers.size());
    info.ppEnabledLayerNames = layers.data();
    info.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size());
    info.ppEnabledExtensionNames = extensions.data();

    check(vkCreateInstance(&info, nullptr, &instance_), "create the Vulkan instance");
    volkLoadInstance(instance_);

    if (validation) {
        const VkResult result = vkCreateDebugUtilsMessengerEXT(instance_, &debug_info, nullptr, &messenger_);
        if (result != VK_SUCCESS) {
            vkDestroyInstance(instance_, nullptr);
            throw_vk_error(result, "create the validation messenger");
        }
    }

    log_info("Vulkan instance created (loader {}.{}.{}, validation {})",
             VK_API_VERSION_MAJOR(loader_version), VK_API_VERSION_MINOR(loader_version),
             VK_API_VERSION_PATCH(loader_version), validation ? "on" : "off");
}

Instance::~Instance() {
    if (messenger_) {
        vkDestroyDebugUtilsMessengerEXT(instance_, messenger_, nullptr);
    }
    vkDestroyInstance(instance_, nullptr);
}

Surface::Surface(const Instance& instance, VkSurfaceKHR surface) noexcept
    : instance_(instance.handle()), surface_(surface) {}

Surface::~Surface() {
    vkDestroySurfaceKHR(instance_, surface_, nullptr);
}

} // namespace elg::detail::gpu
