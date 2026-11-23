// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// GPU's backend (gpu/fence_backend.h) on macOS. macOS has no Vulkan of its own to share: Metal's fences are shared events.

#include "gpu/fence_backend.h"

namespace elg::detail::gpu {

bool vulkan_fences_are_timelines() noexcept {
    return false;
}

} // namespace elg::detail::gpu
