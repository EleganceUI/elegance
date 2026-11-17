// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_GPU_SYSTEM_H
#define ELG_GPU_SYSTEM_H

// What setting up WebGPU needs from the operating system and its graphics libraries. Each
// platform has its own (gpu/windows/system.cpp; gpu/system.cpp elsewhere).

#include "gpu/webgpu_types.h"

#include <array>
#include <cstdint>
#include <optional>
#include <span>

namespace elg::detail::gpu {

// The instance `descriptor` describes, with what Dawn needs to find the system's graphics
// libraries (Windows: System32, for the Direct3D 11 shader compiler and the Vulkan loader).
wgpu::Instance create_instance(const wgpu::InstanceDescriptor& descriptor);

// The Dawn toggles to disable on adapters (Windows: the DirectX shader compiler, when it isn't
// there to load). Logs why.
std::span<const char* const> disabled_adapter_toggles();

// The LUID of the adapter with these PCI vendor and device IDs, on Windows: Direct3D devices that
// share textures with this one (video decoding, composition) must run on the same adapter.
std::optional<std::array<std::uint8_t, 8>> adapter_luid(std::uint32_t vendor, std::uint32_t device);

} // namespace elg::detail::gpu

#endif // ELG_GPU_SYSTEM_H
