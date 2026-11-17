// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Where WebGPU needs nothing of the operating system: every platform but Windows
// (gpu/windows/system.cpp).

#include "gpu/system.h"

#include <webgpu/webgpu_cpp.h>

namespace elg::detail::gpu {

wgpu::Instance create_instance(const wgpu::InstanceDescriptor& descriptor) {
    return wgpu::CreateInstance(&descriptor);
}

std::span<const char* const> disabled_adapter_toggles() {
    return {};
}

std::optional<std::array<std::uint8_t, 8>> adapter_luid(std::uint32_t /*vendor*/, std::uint32_t /*device*/) {
    return std::nullopt;
}

} // namespace elg::detail::gpu
