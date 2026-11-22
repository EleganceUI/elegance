// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include "gpu/dawn_cache.h"

#include "gpu/pipeline_cache.h"
#include "gpu/webgpu.h"

#include <elg/app.h>

#include <webgpu/webgpu_cpp.h>

#include <atomic>
#include <bit>
#include <cstddef>
#include <format>
#include <mutex>
#include <span>
#include <unordered_map>
#include <utility>

#ifndef ELG_DAWN_BUILD
#error "ELG_DAWN_BUILD is the hash of the dawn port's files, which src/CMakeLists.txt defines for this file"
#endif

namespace elg::detail::gpu {

// What the callbacks need, held by the cache and, for the length of a call, by the callback.
struct DawnCache::Shared {
    std::shared_ptr<PipelineCache> cache;
    std::string identity;
};

// The descriptor with its callbacks in the open, which the wrapper keeps to itself.
struct DawnCache::Chain {
    struct Descriptor : wgpu::DawnCacheDeviceDescriptor {
        using wgpu::DawnCacheDeviceDescriptor::dawnLoadCacheDataCallbackInfo;
        using wgpu::DawnCacheDeviceDescriptor::dawnStoreCacheDataCallbackInfo;
    };
    Descriptor descriptor;
};

namespace {

// Dawn's callbacks can run after the device that made them is gone, from a handle that something retains, and
// they take a pointer as their state. That pointer is a token for this registry instead of the state itself, so
// that a late call finds nothing, and doesn't find what was freed. The registry is kept for the process, for
// calls that come while it ends.
struct Registry {
    std::mutex mutex;
    std::unordered_map<std::uintptr_t, std::weak_ptr<DawnCache::Shared>> caches;
};

Registry& registry() {
    static auto* const registry = new Registry;
    return *registry;
}

std::shared_ptr<DawnCache::Shared> find(void* token) {
    Registry& caches = registry();
    const std::lock_guard lock(caches.mutex);
    const auto found = caches.caches.find(std::bit_cast<std::uintptr_t>(token));
    return found == caches.caches.end() ? nullptr : found->second.lock();
}

std::string identity_of(const wgpu::Adapter& adapter) {
    wgpu::AdapterInfo info;
    adapter.GetInfo(&info);
    // The Vulkan driver's version is in a structure that only a Vulkan adapter fills in.
    std::uint32_t vulkan_driver = 0;
    if (info.backendType == wgpu::BackendType::Vulkan) {
        wgpu::AdapterPropertiesVk vulkan;
        wgpu::AdapterInfo with_vulkan;
        with_vulkan.nextInChain = &vulkan;
        adapter.GetInfo(&with_vulkan);
        vulkan_driver = vulkan.driverVersion;
    }
    return std::format("elegance {}\ndawn {}\nbackend {}\nvendor {:#x} {}\narchitecture {}\ndevice {:#x} {}\n"
                       "description {}\nadapter type {}\nvulkan driver {}",
                       version(), ELG_DAWN_BUILD, to_string(info.backendType), info.vendorID, to_string(info.vendor),
                       to_string(info.architecture), info.deviceID, to_string(info.device),
                       to_string(info.description), static_cast<std::uint32_t>(info.adapterType), vulkan_driver);
}

// C callbacks: nothing may unwind through Dawn, and a cache that can't answer is a miss.
std::size_t load_entry(std::size_t key_size, const std::uint8_t* key, std::size_t value_size, std::uint8_t* value,
                       void* token, void* /*unused*/) noexcept {
    try {
        if (const auto shared = find(token)) {
            return shared->cache->load(shared->identity, std::as_bytes(std::span(key, key_size)),
                                       std::as_writable_bytes(std::span(value, value_size)));
        }
    } catch (...) {
        // Swallowed: a C callback; an entry that isn't there is only compiled again.
    }
    return 0;
}

void store_entry(std::size_t key_size, const std::uint8_t* key, std::size_t value_size, const std::uint8_t* value,
                 void* token, void* /*unused*/) noexcept {
    try {
        if (const auto shared = find(token)) {
            shared->cache->store(shared->identity, std::as_bytes(std::span(key, key_size)),
                                 std::as_bytes(std::span(value, value_size)));
        }
    } catch (...) {
        // Swallowed: a C callback; an entry that isn't kept is only compiled again.
    }
}

} // namespace

DawnCache::DawnCache(std::shared_ptr<PipelineCache> cache, const wgpu::Adapter& adapter)
    : shared_(std::make_shared<Shared>(Shared{std::move(cache), identity_of(adapter)})), chain_(std::make_unique<Chain>()) {
    static std::atomic<std::uintptr_t> next_token{1};
    token_ = next_token.fetch_add(1);
    {
        Registry& caches = registry();
        const std::lock_guard lock(caches.mutex);
        caches.caches.emplace(token_, shared_);
    }
    // The wrapper's setters don't take callbacks that have buffers, so the callbacks are given as Dawn's own.
    chain_->descriptor.dawnLoadCacheDataCallbackInfo.callback = &load_entry;
    chain_->descriptor.dawnLoadCacheDataCallbackInfo.userdata1 = std::bit_cast<void*>(token_);
    chain_->descriptor.dawnStoreCacheDataCallbackInfo.callback = &store_entry;
    chain_->descriptor.dawnStoreCacheDataCallbackInfo.userdata1 = std::bit_cast<void*>(token_);
}

DawnCache::~DawnCache() {
    Registry& caches = registry();
    const std::lock_guard lock(caches.mutex);
    caches.caches.erase(token_);
}

void DawnCache::attach(wgpu::DeviceDescriptor& descriptor) {
    chain_->descriptor.nextInChain = descriptor.nextInChain;
    descriptor.nextInChain = &chain_->descriptor;
}

const std::string& DawnCache::identity() const noexcept {
    return shared_->identity;
}

} // namespace elg::detail::gpu
