// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_MEDIA_DECODED_IMAGE_H
#define ELG_MEDIA_DECODED_IMAGE_H

#include <cstdint>
#include <vector>

namespace elg::detail {

// Decoded pixels: RGBA8, sRGB, straight (not premultiplied) alpha, rows top to bottom. Immutable
// once made, so frames on the render thread can share them. `id` is unique per decode (the GPU
// texture cache's key).
struct DecodedImage {
    std::uint64_t id = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> rgba;
};

} // namespace elg::detail

#endif // ELG_MEDIA_DECODED_IMAGE_H
