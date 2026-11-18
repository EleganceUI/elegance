// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include "media/recorded_orientation.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <string_view>

namespace elg::detail {

namespace {

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

// The EXIF orientation of a WebP file (its EXIF chunk, whose TIFF data some writers put after
// "Exif\0\0"), or 1.
int webp_orientation(std::span<const std::uint8_t> data) {
    std::size_t at = 12; // past "RIFF", the size and "WEBP"
    while (data.size() - at >= 8) { // a chunk's type and size
        const auto length = read_u32(data, at + 4, true);
        if (!length || *length > data.size() - at - 8) {
            return 1; // cut short
        }
        if (std::ranges::equal(data.subspan(at, 4), std::string_view("EXIF"))) {
            std::span<const std::uint8_t> exif = data.subspan(at + 8, *length);
            if (exif.size() >= 6 && std::ranges::equal(exif.first(6), std::string_view("Exif\0\0", 6))) {
                exif = exif.subspan(6);
            }
            return tiff_orientation(exif);
        }
        at += 8 + std::size_t{*length} + (*length & 1); // chunks are padded to even sizes
    }
    return 1;
}

} // namespace

int recorded_orientation(std::span<const std::uint8_t> data) {
    static constexpr std::array<std::uint8_t, 8> kPngSignature{0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    if (data.size() >= 4 && data[0] == 0xFF && data[1] == 0xD8) {
        return jpeg_orientation(data);
    }
    if (data.size() >= kPngSignature.size() && std::ranges::equal(data.first(kPngSignature.size()), kPngSignature)) {
        return png_orientation(data);
    }
    if (data.size() >= 12 && std::ranges::equal(data.first(4), std::string_view("RIFF")) &&
        std::ranges::equal(data.subspan(8, 4), std::string_view("WEBP"))) {
        return webp_orientation(data);
    }
    return 1;
}

} // namespace elg::detail
