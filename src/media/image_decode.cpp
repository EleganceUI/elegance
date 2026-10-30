// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Image decoding (design.md §13.3): the platform's codecs, in platform/<os>/image_codec.*: the
// Windows Imaging Component, ImageIO on macOS, stb_image on Linux. Orientation is applied here.

#include "media/image_decode.h"

#include <elg/error.h>

#include <algorithm>
#include <atomic>
#include <utility>

namespace elg::detail {

namespace {

std::atomic<std::uint64_t> g_next_id{1};

// EXIF orientation 1-8: whether the stored pixels are turned a quarter (the upright size swaps).
bool swaps_axes(int orientation) {
    return orientation >= 5 && orientation <= 8;
}

// Applies EXIF orientation `orientation` to RGBA pixels (w x h, as stored).
void orient(DecodedImage& image, int orientation) {
    if (orientation <= 1 || orientation > 8) {
        return;
    }
    const std::uint32_t w = image.width;
    const std::uint32_t h = image.height;
    const bool swap = swaps_axes(orientation);
    const std::uint32_t out_w = swap ? h : w;
    const std::uint32_t out_h = swap ? w : h;
    std::vector<std::uint8_t> out(image.rgba.size());
    const auto* in = reinterpret_cast<const std::uint32_t*>(image.rgba.data());
    auto* dst = reinterpret_cast<std::uint32_t*>(out.data());
    for (std::uint32_t y = 0; y < out_h; ++y) {
        for (std::uint32_t x = 0; x < out_w; ++x) {
            // The stored pixel shown at (x, y).
            std::uint32_t sx = x;
            std::uint32_t sy = y;
            switch (orientation) {
            case 2: sx = w - 1 - x; sy = y; break;         // mirrored
            case 3: sx = w - 1 - x; sy = h - 1 - y; break; // upside down
            case 4: sx = x; sy = h - 1 - y; break;         // mirrored, upside down
            case 5: sx = y; sy = x; break;                 // transposed
            case 6: sx = y; sy = h - 1 - x; break;         // turned right: rotate 90 clockwise
            case 7: sx = w - 1 - y; sy = h - 1 - x; break; // transversed
            case 8: sx = w - 1 - y; sy = x; break;         // turned left: rotate 90 counter-clockwise
            default: break;
            }
            dst[y * out_w + x] = in[sy * w + sx];
        }
    }
    image.rgba = std::move(out);
    image.width = out_w;
    image.height = out_h;
}

} // namespace

ImageInfo probe_image(std::span<const std::uint8_t> data) {
    const StoredImageInfo info = platform_probe_image(data);
    return swaps_axes(info.orientation) ? ImageInfo{info.height, info.width} : ImageInfo{info.width, info.height};
}

std::shared_ptr<const DecodedImage> decode_image(std::span<const std::uint8_t> data, std::uint32_t max_size) {
    auto image = std::make_shared<DecodedImage>();
    image->id = g_next_id.fetch_add(1);
    const int orientation = platform_decode_image(data, max_size, *image);
    orient(*image, orientation);
    return image;
}

} // namespace elg::detail
