// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_GPU_FENCE_BACKEND_H
#define ELG_GPU_FENCE_BACKEND_H

// GPU's backend interface (ELGSPEC-0001.R4): what the system's Vulkan shares with the producers of GPU images
// (include/elg/gpu_image.h), implemented in platform/<os>/gpu_fences.cpp.

namespace elg::detail::gpu {

// Whether the Vulkan fences that a producer shares with Elegance are timeline semaphores, signaled to values that
// grow, and not binary semaphores, which are signaled once: on Mikros, whose Vulkan has no binary semaphores to share.
bool vulkan_fences_are_timelines() noexcept;

} // namespace elg::detail::gpu

#endif // ELG_GPU_FENCE_BACKEND_H
