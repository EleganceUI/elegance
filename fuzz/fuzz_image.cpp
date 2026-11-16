// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Image data: probing (the size and EXIF orientation) and decoding, which may only fail with
// ResourceError. Decoding gets lower limits than the defaults, so an input can't ask for more
// memory than the fuzzer allows.

#include "media/image_decode.h"

#include <elg/error.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size);

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::span<const std::uint8_t> bytes(data, size);
    const elg::detail::ImageLimits limits{.max_pixels = std::uint64_t{1} << 22, .max_side = 1024};
    try {
        static_cast<void>(elg::detail::probe_image(bytes, limits));
        const auto image = elg::detail::decode_image(bytes, limits);
        // As decoded: within the longest side, with a pixel for each 4 bytes.
        if (image->width > limits.max_side || image->height > limits.max_side ||
            image->rgba.size() != std::size_t{image->width} * image->height * 4) {
            std::abort();
        }
    } catch (const elg::ResourceError&) {
        // Refused: not an image this platform decodes, or over the limits.
    }
    return 0;
}
