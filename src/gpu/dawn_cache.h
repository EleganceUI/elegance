// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_GPU_DAWN_CACHE_H
#define ELG_GPU_DAWN_CACHE_H

// What joins the pipeline cache (gpu/pipeline_cache.h) to the WebGPU implementation (ELGSPEC-0036.R3): the
// callbacks that Dawn asks for and offers its compiled shaders and pipelines through, and the identity that
// every entry is stored under (ELGSPEC-0036.R5). Declared with WebGPU's types incomplete
// (gpu/webgpu_types.h): the sources that use it include <webgpu/webgpu_cpp.h>.

#include "gpu/webgpu_types.h"

#include <cstdint>
#include <memory>
#include <string>

namespace elg::detail::gpu {

class PipelineCache;

class DawnCache {
public:
    // Joins `cache` to the device that `adapter` makes. The identity of its entries is the Elegance version, the
    // WebGPU implementation's build, the graphics API, and the GPU and the driver that `adapter` is.
    DawnCache(std::shared_ptr<PipelineCache> cache, const wgpu::Adapter& adapter);
    // From here on, what Dawn asks for or offers, from a device that is still alive in a handle somewhere, is
    // answered with nothing, and not kept.
    ~DawnCache();

    DawnCache(const DawnCache&) = delete;
    DawnCache& operator=(const DawnCache&) = delete;
    DawnCache(DawnCache&&) = delete;
    DawnCache& operator=(DawnCache&&) = delete;

    // Chains the callbacks in front of `descriptor`'s chain. They live as long as this does, and so must the
    // descriptor's use.
    void attach(wgpu::DeviceDescriptor& descriptor);

    [[nodiscard]] const std::string& identity() const noexcept;

    // What the callbacks need (gpu/dawn_cache.cpp).
    struct Shared;

private:
    struct Chain;
    std::shared_ptr<Shared> shared_;
    std::unique_ptr<Chain> chain_;
    std::uintptr_t token_ = 0;
};

} // namespace elg::detail::gpu

#endif // ELG_GPU_DAWN_CACHE_H
