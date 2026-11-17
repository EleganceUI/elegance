// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_GPU_NATIVE_IMAGE_H
#define ELG_GPU_NATIVE_IMAGE_H

// Images the system's decoders make in GPU memory, imported into the renderer's device as they
// are rather than copied into memory and uploaded again. macOS's graphics interop has them
// (gpu/macos/native_image.mm: IOSurface-backed CVPixelBuffers); elsewhere (gpu/native_image.cpp)
// there are none.

#include <cstdint>
#include <memory>

namespace elg::detail::gpu {

class ExternalImage; // gpu/external_image.h

// Whether the running renderer can import such images now.
bool imports_native_images();

// An imported image, with its size in pixels; a null `image` if it couldn't be imported.
struct NativeImport {
    std::shared_ptr<ExternalImage> image;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};
// Imports `image` (macOS: a BGRA CVPixelBufferRef backed by an IOSurface) into the renderer's
// device, beginning access to it. Retains it until the image is released, including the GPU's
// reads; if it can't be imported, nothing is retained.
NativeImport import_native_image(void* image);

} // namespace elg::detail::gpu

#endif // ELG_GPU_NATIVE_IMAGE_H
