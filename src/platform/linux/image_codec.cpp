// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Images on Linux (media/image_decode.h): stb_image, stb_image_resize2 and stb_image_write, with
// the EXIF orientation of JPEG and PNG files read here.

#include "media/image_decode.h"

#include <elg/error.h>

#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <cstring>
#include <format>
#include <optional>
#include <span>
#include <string_view>

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

// Every read below is checked against the data: it comes from files and the network.

std::optional<std::uint32_t> read_u16(std::span<const std::uint8_t> data, std::size_t at, bool little) {
    if (at > data.size() || data.size() - at < 2) {
        return std::nullopt;
    }
    return little ? data[at] | (data[at + 1] << 8) : (data[at] << 8) | data[at + 1];
}

std::optional<std::uint32_t> read_u32(std::span<const std::uint8_t> data, std::size_t at, bool little) {
    const auto low = read_u16(data, at + (little ? 0 : 2), little);
    const auto high = read_u16(data, at + (little ? 2 : 0), little);
    if (!low || !high) {
        return std::nullopt;
    }
    return *low | (*high << 16);
}

// The orientation (1-8) in EXIF data, a TIFF structure ("II*\0" or "MM\0*", then the offset of
// its first IFD), or 1.
int tiff_orientation(std::span<const std::uint8_t> tiff) {
    if (tiff.size() < 8) {
        return 1;
    }
    const bool little = tiff[0] == 'I' && tiff[1] == 'I';
    if (!little && (tiff[0] != 'M' || tiff[1] != 'M')) {
        return 1;
    }
    const auto ifd = read_u32(tiff, 4, little);
    const auto count = ifd ? read_u16(tiff, *ifd, little) : std::nullopt;
    if (!count || (tiff.size() - *ifd - 2) / 12 < *count) { // read_u16 checked that *ifd + 2 fits
        return 1;
    }
    for (std::uint32_t i = 0; i < *count; ++i) {
        const std::size_t entry = *ifd + 2 + std::size_t{i} * 12;
        if (read_u16(tiff, entry, little) == 0x0112u && read_u16(tiff, entry + 2, little) == 3u) { // Orientation, SHORT
            const auto value = read_u16(tiff, entry + 8, little);
            return value && *value >= 1 && *value <= 8 ? static_cast<int>(*value) : 1;
        }
    }
    return 1;
}

// The EXIF orientation of a JPEG file (an APP1 segment before the image data), or 1.
int jpeg_orientation(std::span<const std::uint8_t> data) {
    std::size_t at = 2; // past the start of image (FF D8)
    while (at < data.size() && data.size() - at >= 4 && data[at] == 0xFF) {
        const std::uint8_t marker = data[at + 1];
        if (marker == 0xD9 || marker == 0xDA) {
            break; // the end, or the image data: no more metadata
        }
        const auto length = read_u16(data, at + 2, false);
        if (!length || *length < 2) {
            break;
        }
        const std::size_t start = at + 4;
        const std::size_t end = std::min(data.size(), at + 2 + *length);
        if (marker == 0xE1 && end - start >= 6 && std::memcmp(data.data() + start, "Exif\0\0", 6) == 0) {
            return tiff_orientation(data.subspan(start + 6, end - start - 6));
        }
        at = at + 2 + *length;
    }
    return 1;
}

// The EXIF orientation of a PNG file (its eXIf chunk), or 1. Chunks are read until IEND, wherever
// the eXIf one is.
int png_orientation(std::span<const std::uint8_t> data) {
    std::size_t at = 8; // past the signature
    while (data.size() - at >= 12) { // a chunk's length, type and CRC
        const auto length = read_u32(data, at, false);
        if (!length || *length > data.size() - at - 12) {
            return 1; // cut short
        }
        const std::span<const std::uint8_t> type = data.subspan(at + 4, 4);
        if (std::ranges::equal(type, std::string_view("eXIf"))) {
            return tiff_orientation(data.subspan(at + 8, *length));
        }
        if (std::ranges::equal(type, std::string_view("IEND"))) {
            break;
        }
        at += 12 + std::size_t{*length};
    }
    return 1;
}

// The EXIF orientation (1-8) of a JPEG or PNG file, or 1 (other formats, or none recorded).
int exif_orientation(std::span<const std::uint8_t> data) {
    static constexpr std::array<std::uint8_t, 8> kPngSignature{0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    if (data.size() >= 4 && data[0] == 0xFF && data[1] == 0xD8) {
        return jpeg_orientation(data);
    }
    if (data.size() >= kPngSignature.size() && std::ranges::equal(data.first(kPngSignature.size()), kPngSignature)) {
        return png_orientation(data);
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
    return {static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height), exif_orientation(data)};
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
    return exif_orientation(data);
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
