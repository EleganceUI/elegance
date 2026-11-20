// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Image decoding (design.md §13.3): the platform's codecs, in platform/<os>/image_codec.*: the
// Windows Imaging Component, ImageIO on macOS, stb_image on Linux. Orientation is applied here.

#include "media/image_decode.h"

#include "core/profile.h"
#include "media/image_codec_backend.h"
#include "media/recorded_orientation.h"

#include <elg/error.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <format>
#include <utility>

namespace elg::detail {

namespace {

std::atomic<std::uint64_t> g_next_id{1};
std::atomic<std::uint32_t> g_max_texture_side{ImageLimits::kDefaultMaxSide};

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
    if (image.rgba.size() < std::size_t{w} * h * 4) {
        throw ResourceError("the decoder returned fewer pixels than the image's size");
    }
    const bool swap = swaps_axes(orientation);
    const std::uint32_t out_w = swap ? h : w;
    const std::uint32_t out_h = swap ? w : h;
    std::vector<std::uint8_t> out(image.rgba.size());
    const std::uint8_t* const in = image.rgba.data();
    std::uint8_t* const dst = out.data();
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
            std::memcpy(dst + (std::size_t{y} * out_w + x) * 4, in + (std::size_t{sy} * w + sx) * 4, 4);
        }
    }
    image.rgba = std::move(out);
    image.width = out_w;
    image.height = out_h;
}

// The codec's orientation, else the one the file records that the codec didn't read
// (ELGSPEC-0005.R6): the same file then shows the same way on every platform.
int orientation_of(int reported, std::span<const std::uint8_t> data) {
    return reported >= 2 && reported <= 8 ? reported : recorded_orientation(data);
}

// The size and orientation of an image within `limits`.
StoredImageInfo probe_within(std::span<const std::uint8_t> data, const ImageLimits& limits) {
    StoredImageInfo info = platform_probe_image(data);
    info.orientation = orientation_of(info.orientation, data);
    if (std::uint64_t{info.width} * info.height > limits.max_pixels) {
        throw ResourceError(std::format("the image is {} x {} pixels, more than the {} an image may have", info.width,
                                        info.height, limits.max_pixels));
    }
    return info;
}

} // namespace

std::uint32_t max_texture_side() noexcept {
    return g_max_texture_side.load();
}

void set_max_texture_side(std::uint32_t side) noexcept {
    g_max_texture_side = std::max(side, 1u);
}

ImageInfo probe_image(std::span<const std::uint8_t> data, const ImageLimits& limits) {
    const StoredImageInfo info = probe_within(data, limits);
    return swaps_axes(info.orientation) ? ImageInfo{info.height, info.width} : ImageInfo{info.width, info.height};
}

std::shared_ptr<const DecodedImage> decode_image(std::span<const std::uint8_t> data, const ImageLimits& limits) {
    ELG_ZONE("Decode image");
    probe_within(data, limits);
    auto image = std::make_shared<DecodedImage>();
    image->id = g_next_id.fetch_add(1);
    const int orientation = platform_decode_image(data, std::min(limits.max_side, max_texture_side()), *image);
    orient(*image, orientation_of(orientation, data));
    return image;
}

} // namespace elg::detail
