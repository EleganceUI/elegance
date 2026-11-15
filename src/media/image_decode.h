// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_MEDIA_IMAGE_DECODE_H
#define ELG_MEDIA_IMAGE_DECODE_H

#include <cstdint>
#include <memory>
#include <optional>
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

// Throws elg::ResourceError if the data isn't an image the platform can decode (PNG, JPEG, GIF,
// BMP, TIFF, ICO everywhere; WebP, HEIF, AVIF where the OS has the codecs), or if it has more
// pixels than `limits` allow (the message names the limit).
ImageInfo probe_image(std::span<const std::uint8_t> data, const ImageLimits& limits = {});
// Decodes, applying the EXIF orientation, within `limits`; throws as probe_image does.
// Thread-safe.
std::shared_ptr<const DecodedImage> decode_image(std::span<const std::uint8_t> data, const ImageLimits& limits = {});

// --- Each OS's codecs (platform/<os>/image_codec.*) ---

// The size as stored and the EXIF orientation (1-8; 1 when absent).
struct StoredImageInfo {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    int orientation = 1;
};
// Throw elg::ResourceError as probe_image and decode_image do. Decoding fills `image`'s size and
// pixels as stored (not yet oriented) and returns the orientation.
StoredImageInfo platform_probe_image(std::span<const std::uint8_t> data);
int platform_decode_image(std::span<const std::uint8_t> data, std::uint32_t max_size, DecodedImage& image);
// Opaque BGRA pixels (width * 4 bytes a row, top to bottom) as a PNG file, or nullopt on failure.
std::optional<std::vector<unsigned char>> encode_png(std::span<const std::uint8_t> bgra, std::uint32_t width,
                                                     std::uint32_t height);

} // namespace elg::detail

#endif // ELG_MEDIA_IMAGE_DECODE_H
