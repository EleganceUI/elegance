// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Where the system's decoders hand over no images in GPU memory: every platform but macOS
// (gpu/macos/native_image.mm).

#include "gpu/native_image.h"

namespace elg::detail::gpu {

bool imports_native_images() {
    return false;
}

NativeImport import_native_image(void* /*image*/) {
    return {};
}

} // namespace elg::detail::gpu
