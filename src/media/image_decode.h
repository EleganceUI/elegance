// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_MEDIA_IMAGE_DECODE_H
#define ELG_MEDIA_IMAGE_DECODE_H

#include <cstdint>
#include <memory>
#include <span>
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

// The upright size (after the EXIF orientation), read from the header without decoding.
struct ImageInfo {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

// Throws elg::ResourceError if the data isn't an image the platform can decode (PNG, JPEG, GIF,
// BMP, TIFF, ICO everywhere; WebP, HEIF, AVIF where the OS has the codecs).
ImageInfo probe_image(std::span<const std::uint8_t> data);
// Decodes, applying the EXIF orientation. Images larger than `max_size` on a side are scaled
// down to fit (GPU texture limits). Thread-safe.
std::shared_ptr<const DecodedImage> decode_image(std::span<const std::uint8_t> data, std::uint32_t max_size = 8192);

} // namespace elg::detail

#endif // ELG_MEDIA_IMAGE_DECODE_H
