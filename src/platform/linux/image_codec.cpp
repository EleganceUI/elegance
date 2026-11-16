// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Images on Linux (media/image_decode.h): stb_image and stb_image_write, with the EXIF orientation
// of JPEG and PNG files read here, and images scaled down here too (stb_image_resize2 stores
// floats through misaligned 64-bit pointers).

#include "media/image_decode.h"

#include <elg/error.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <climits>
#include <cmath>
#include <cstdarg>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <format>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace {

// stb_image copies 0 bytes from or to a null pointer in places (an empty first IDAT chunk), which
// memcpy and memset don't allow. It gets these, which do nothing for 0 bytes.
void* stb_copy(void* to, const void* from, std::size_t size) noexcept {
    return size == 0 ? to : std::memcpy(to, from, size);
}
void* stb_fill(void* to, int value, std::size_t size) noexcept {
    return size == 0 ? to : std::memset(to, value, size);
}

} // namespace

// Internal linkage: an app that uses stb too gets no duplicate symbols. The standard headers stb
// includes are included above, so the macros only reach stb's own code.
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define memcpy(to, from, size) stb_copy(to, from, size)
#define memset(to, value, size) stb_fill(to, value, size)
#include <stb_image.h>
#undef memcpy
#undef memset
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

// RGBA pixels (w x h, 4 bytes each) scaled down to out_w x out_h, neither larger than the source:
// each output pixel averages the source pixels in its share of the image, colors weighted by
// alpha so transparent pixels don't tint their neighbors. The shares split the source rows and
// columns between the output's, so each source pixel is read once. Integer sums: an average of
// up to 2^40 pixels can't overflow.
void scale_down(const std::uint8_t* pixels, std::uint32_t w, std::uint32_t h, std::uint32_t out_w, std::uint32_t out_h,
                std::vector<std::uint8_t>& out) {
    const auto share = [](std::uint32_t i, std::uint32_t from, std::uint32_t to) {
        return static_cast<std::uint32_t>(std::uint64_t{i} * from / to); // where output i's share starts
    };
    std::vector<std::uint32_t> columns(out_w + 1);
    for (std::uint32_t x = 0; x <= out_w; ++x) {
        columns[x] = share(x, w, out_w);
    }
    out.assign(std::size_t{out_w} * out_h * 4, 0);
    std::vector<std::uint64_t> sums(std::size_t{out_w} * 4); // alpha-weighted red, green, blue, then alpha
    for (std::uint32_t oy = 0; oy < out_h; ++oy) {
        const std::uint32_t y0 = share(oy, h, out_h);
        const std::uint32_t y1 = std::max(y0 + 1, share(oy + 1, h, out_h));
        std::ranges::fill(sums, 0);
        for (std::uint32_t y = y0; y < y1; ++y) {
            const std::uint8_t* row = pixels + std::size_t{y} * w * 4;
            for (std::uint32_t ox = 0; ox < out_w; ++ox) {
                std::uint64_t* sum = &sums[std::size_t{ox} * 4];
                for (std::uint32_t x = columns[ox]; x < std::max(columns[ox] + 1, columns[ox + 1]); ++x) {
                    const std::uint8_t* p = row + std::size_t{x} * 4;
                    sum[0] += std::uint64_t{p[0]} * p[3];
                    sum[1] += std::uint64_t{p[1]} * p[3];
                    sum[2] += std::uint64_t{p[2]} * p[3];
                    sum[3] += p[3];
                }
            }
        }
        for (std::uint32_t ox = 0; ox < out_w; ++ox) {
            const std::uint64_t* sum = &sums[std::size_t{ox} * 4];
            const std::uint64_t count = std::uint64_t{y1 - y0} * std::max(1u, columns[ox + 1] - columns[ox]);
            std::uint8_t* q = &out[(std::size_t{oy} * out_w + ox) * 4];
            for (int c = 0; c < 3; ++c) {
                q[c] = sum[3] == 0 ? 0 : static_cast<std::uint8_t>((sum[c] + sum[3] / 2) / sum[3]);
            }
            q[3] = static_cast<std::uint8_t>((sum[3] + count / 2) / count);
        }
    }
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
    if (width <= 0 || height <= 0) {
        throw ResourceError("the image has no pixels"); // some formats let stb read a size of 0
    }
    return {static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height), exif_orientation(data)};
}

int platform_decode_image(std::span<const std::uint8_t> data, std::uint32_t max_size, DecodedImage& image) {
    int width = 0;
    int height = 0;
    int channels = 0;
    const std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels(
        stbi_load_from_memory(data.data(), stb_length(data), &width, &height, &channels, 4), &stbi_image_free);
    if (!pixels) {
        throw ResourceError(std::format("can't decode the image: {}", stbi_failure_reason()));
    }
    if (width <= 0 || height <= 0) {
        throw ResourceError("the image has no pixels");
    }
    const auto w = static_cast<std::uint32_t>(width);
    const auto h = static_cast<std::uint32_t>(height);
    if (max_size > 0 && (w > max_size || h > max_size)) {
        // Scaled down to fit, keeping the aspect ratio.
        const double k = static_cast<double>(max_size) / std::max(w, h);
        const auto out_w = std::clamp(static_cast<std::uint32_t>(std::lround(w * k)), 1u, w);
        const auto out_h = std::clamp(static_cast<std::uint32_t>(std::lround(h * k)), 1u, h);
        scale_down(pixels.get(), w, h, out_w, out_h, image.rgba);
        image.width = out_w;
        image.height = out_h;
    } else {
        image.width = w;
        image.height = h;
        image.rgba.assign(pixels.get(), pixels.get() + std::size_t{w} * h * 4);
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
