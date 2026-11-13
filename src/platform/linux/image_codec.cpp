// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Images on Linux (media/image_decode.h): stb_image, stb_image_resize2 and stb_image_write, with
// the EXIF orientation of JPEG files read here.

#include "media/image_decode.h"

#include <elg/error.h>

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstring>
#include <format>
#include <optional>

// Internal linkage: an app that uses stb too gets no duplicate symbols.
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#include <stb_image.h>
#define STB_IMAGE_RESIZE_STATIC
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include <stb_image_resize2.h>
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include <stb_image_write.h>

namespace elg::detail {

namespace {

// stb takes the data's length as an int.
int stb_length(std::span<const std::uint8_t> data) {
    if (data.size() > static_cast<std::size_t>(INT_MAX)) {
        throw ResourceError("the image data is larger than 2 GB");
    }
    return static_cast<int>(data.size());
}

// The EXIF orientation (1-8) of a JPEG file, or 1. Every read is checked against the data: it
// comes from files and the network.
int jpeg_orientation(std::span<const std::uint8_t> data) {
    const auto u16 = [&](std::size_t at, bool little) -> std::optional<std::uint32_t> {
        if (at + 2 > data.size()) {
            return std::nullopt;
        }
        return little ? data[at] | (data[at + 1] << 8) : (data[at] << 8) | data[at + 1];
    };
    const auto u32 = [&](std::size_t at, bool little) -> std::optional<std::uint32_t> {
        const auto low = u16(at + (little ? 0 : 2), little);
        const auto high = u16(at + (little ? 2 : 0), little);
        if (!low || !high) {
            return std::nullopt;
        }
        return *low | (*high << 16);
    };
    if (data.size() < 4 || data[0] != 0xFF || data[1] != 0xD8) {
        return 1; // not a JPEG
    }
    std::size_t at = 2;
    while (at + 4 <= data.size() && data[at] == 0xFF) {
        const std::uint8_t marker = data[at + 1];
        if (marker == 0xD9 || marker == 0xDA) {
            break; // the end, or the image data: no more metadata
        }
        const auto length = u16(at + 2, false);
        if (!length || *length < 2) {
            break;
        }
        const std::size_t start = at + 4;
        const std::size_t end = std::min(data.size(), at + 2 + *length);
        if (marker == 0xE1 && end - start >= 14 && std::memcmp(data.data() + start, "Exif\0\0", 6) == 0) {
            const std::size_t tiff = start + 6;
            const bool little = data[tiff] == 'I' && data[tiff + 1] == 'I';
            if (!little && (data[tiff] != 'M' || data[tiff + 1] != 'M')) {
                return 1;
            }
            const auto ifd = u32(tiff + 4, little);
            if (!ifd || *ifd > end - tiff) {
                return 1;
            }
            const std::size_t entries_at = tiff + *ifd;
            const auto count = u16(entries_at, little);
            if (!count || entries_at + 2 + std::size_t{*count} * 12 > end) {
                return 1;
            }
            for (std::uint32_t i = 0; i < *count; ++i) {
                const std::size_t entry = entries_at + 2 + std::size_t{i} * 12;
                if (u16(entry, little) == 0x0112u && u16(entry + 2, little) == 3u) { // Orientation, SHORT
                    const auto value = u16(entry + 8, little);
                    return value && *value >= 1 && *value <= 8 ? static_cast<int>(*value) : 1;
                }
            }
            return 1;
        }
        at = at + 2 + *length;
    }
    return 1;
}

} // namespace

StoredImageInfo platform_probe_image(std::span<const std::uint8_t> data) {
    int width = 0;
    int height = 0;
    int channels = 0;
    if (!stbi_info_from_memory(data.data(), stb_length(data), &width, &height, &channels)) {
        throw ResourceError(std::format("can't read the image: {}", stbi_failure_reason()));
    }
    return {static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height), jpeg_orientation(data)};
}

int platform_decode_image(std::span<const std::uint8_t> data, std::uint32_t max_size, DecodedImage& image) {
    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_uc* pixels = stbi_load_from_memory(data.data(), stb_length(data), &width, &height, &channels, 4);
    if (!pixels) {
        throw ResourceError(std::format("can't decode the image: {}", stbi_failure_reason()));
    }
    const auto w = static_cast<std::uint32_t>(width);
    const auto h = static_cast<std::uint32_t>(height);
    if (max_size > 0 && (w > max_size || h > max_size)) {
        // Scaled down to fit, keeping the aspect ratio, weighting colors by alpha. (Not the sRGB
        // variant: its encoder indexes a table out of bounds with the filter's negative lobes.)
        const double k = static_cast<double>(max_size) / std::max(w, h);
        const auto out_w = std::max(1u, static_cast<std::uint32_t>(std::lround(w * k)));
        const auto out_h = std::max(1u, static_cast<std::uint32_t>(std::lround(h * k)));
        image.rgba.resize(std::size_t{out_w} * out_h * 4);
        const bool resized = stbir_resize_uint8_linear(pixels, width, height, 0, image.rgba.data(), static_cast<int>(out_w),
                                                     static_cast<int>(out_h), 0, STBIR_RGBA) != nullptr;
        stbi_image_free(pixels);
        if (!resized) {
            throw ResourceError("can't scale the image down");
        }
        image.width = out_w;
        image.height = out_h;
    } else {
        image.width = w;
        image.height = h;
        image.rgba.assign(pixels, pixels + std::size_t{w} * h * 4);
        stbi_image_free(pixels);
    }
    return jpeg_orientation(data);
}

std::optional<std::vector<unsigned char>> encode_png(std::span<const std::uint8_t> bgra, std::uint32_t width,
                                                     std::uint32_t height) {
    std::vector<std::uint8_t> rgba(bgra.begin(), bgra.end());
    for (std::size_t i = 0; i + 3 < rgba.size(); i += 4) {
        std::swap(rgba[i], rgba[i + 2]);
    }
    std::vector<unsigned char> png;
    const auto append = [](void* context, void* data, int size) {
        auto* out = static_cast<std::vector<unsigned char>*>(context);
        const auto* bytes = static_cast<const unsigned char*>(data);
        out->insert(out->end(), bytes, bytes + size);
    };
    if (!stbi_write_png_to_func(append, &png, static_cast<int>(width), static_cast<int>(height), 4, rgba.data(),
                                static_cast<int>(width) * 4)) {
        return std::nullopt;
    }
    return png;
}

} // namespace elg::detail
