// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_MEDIA_IMAGE_CODEC_BACKEND_H
#define ELG_MEDIA_IMAGE_CODEC_BACKEND_H

// Media's backend interface for images (ELGSPEC-0001.R4): each OS's codecs, in
// platform/<os>/image_codec.*. Media applies the limits and the orientation
// (media/image_decode.h).

#include "media/decoded_image.h"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace elg::detail {

// The size as stored and the EXIF orientation (1-8; 1 when absent).
struct StoredImageInfo {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    int orientation = 1;
};
// Throw elg::ResourceError as probe_image and decode_image do. Decoding fills `image`'s size and
// pixels as stored (not yet oriented), scaled down to fit `max_size` on a side, and returns the
// orientation.
StoredImageInfo platform_probe_image(std::span<const std::uint8_t> data);
int platform_decode_image(std::span<const std::uint8_t> data, std::uint32_t max_size, DecodedImage& image);
// Opaque BGRA pixels (width * 4 bytes a row, top to bottom) as a PNG file, or nullopt on failure.
std::optional<std::vector<unsigned char>> encode_png(std::span<const std::uint8_t> bgra, std::uint32_t width,
                                                     std::uint32_t height);

} // namespace elg::detail

#endif // ELG_MEDIA_IMAGE_CODEC_BACKEND_H
