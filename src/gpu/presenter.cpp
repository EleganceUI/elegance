// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Where windows present only through their WebGPU surfaces: every platform but Windows
// (gpu/windows/composition.cpp).

#include "gpu/presenter.h"

#include "gpu/swapchain.h"

#include <memory>

namespace elg::detail::gpu {

std::unique_ptr<Presenter> create_presenter(const Device& device, const SurfaceSource& surface, bool transparent,
                                            void* /*native_window*/) {
    return std::make_unique<Swapchain>(device, surface, transparent);
}

} // namespace elg::detail::gpu
