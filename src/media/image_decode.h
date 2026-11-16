// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_MEDIA_IMAGE_DECODE_H
#define ELG_MEDIA_IMAGE_DECODE_H

#include "media/decoded_image.h"

#include <cstdint>
#include <memory>
#include <span>

namespace elg::detail {

// The upright size (after the EXIF orientation), read from the header without decoding.
struct ImageInfo {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

// The bounds an image is decoded within.
struct ImageLimits {
    static constexpr std::uint64_t kDefaultMaxPixels = std::uint64_t{1} << 27; // 512 MiB of RGBA
    static constexpr std::uint32_t kDefaultMaxSide = 8192;

    // Images with more pixels (as stored) are refused before decoding: a few bytes of header can
    // ask for gigabytes.
    std::uint64_t max_pixels = kDefaultMaxPixels;
    // Images longer than this on a side are scaled down to fit (GPU texture limits).
    std::uint32_t max_side = kDefaultMaxSide;
};

// The limits an app gave (ImageSource, AsyncImageOptions), where 0 means the default.
inline ImageLimits image_limits(std::uint64_t max_pixels, std::uint32_t max_side) {
    return {max_pixels != 0 ? max_pixels : ImageLimits::kDefaultMaxPixels,
            max_side != 0 ? max_side : ImageLimits::kDefaultMaxSide};
}

// The longest side a texture may have on the GPU in use: 8192, which every WebGPU device allows,
// until device creation sets the device's own (gpu/device.cpp). Decoding never goes past it,
// whatever ImageLimits::max_side says. Any thread.
std::uint32_t max_texture_side() noexcept;
void set_max_texture_side(std::uint32_t side) noexcept;

// Throws elg::ResourceError if the data isn't an image the platform can decode (PNG, JPEG, GIF,
// BMP, TIFF, ICO everywhere; WebP, HEIF, AVIF where the OS has the codecs), or if it has more
// pixels than `limits` allow (the message names the limit).
ImageInfo probe_image(std::span<const std::uint8_t> data, const ImageLimits& limits = {});
// Decodes, applying the EXIF orientation, within `limits` and max_texture_side(); throws as
// probe_image does. Thread-safe.
std::shared_ptr<const DecodedImage> decode_image(std::span<const std::uint8_t> data, const ImageLimits& limits = {});

} // namespace elg::detail

#endif // ELG_MEDIA_IMAGE_DECODE_H
