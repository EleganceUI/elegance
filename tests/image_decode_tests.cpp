// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Decoding image data, which comes from files and the network: size limits, scaling to the size
// asked for, and EXIF orientation.

#include "media/image_decode.h"

#include <elg/error.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string_view>
#include <vector>

using namespace elg;
using Catch::Matchers::ContainsSubstring;

namespace {

void append(std::vector<std::uint8_t>& out, std::initializer_list<int> bytes) {
    for (const int byte : bytes) {
        out.push_back(static_cast<std::uint8_t>(byte));
    }
}

// Appends `value` most significant byte first, as PNG stores numbers.
void append_u32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    for (const int shift : {24, 16, 8, 0}) {
        out.push_back(static_cast<std::uint8_t>(value >> shift));
    }
}

// Appends a PNG chunk: its length, type and data, then the CRC-32 of its type and data (ImageIO
// doesn't read a chunk whose CRC is wrong).
void append_chunk(std::vector<std::uint8_t>& png, std::string_view type, const std::vector<std::uint8_t>& data) {
    append_u32(png, static_cast<std::uint32_t>(data.size()));
    const std::size_t start = png.size();
    png.insert(png.end(), type.begin(), type.end());
    png.insert(png.end(), data.begin(), data.end());
    std::uint32_t crc = 0xFFFFFFFF;
    for (std::size_t i = start; i < png.size(); ++i) {
        crc ^= png[i];
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    append_u32(png, ~crc);
}

// A PNG file of a `width` x `height` RGBA image whose image data stops at its start: decoders read
// its size, but it has no pixels to decode.
std::vector<std::uint8_t> png_header(std::uint32_t width, std::uint32_t height) {
    std::vector<std::uint8_t> png;
    append(png, {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A});
    std::vector<std::uint8_t> header;
    append_u32(header, width);
    append_u32(header, height);
    append(header, {8, 6, 0, 0, 0}); // 8-bit RGBA, not interlaced
    append_chunk(png, "IHDR", header);
    append_chunk(png, "IDAT", {0x78, 0x01, 0, 0, 0, 0xFF, 0xFF}); // zlib's header, an empty stored block
    append_chunk(png, "IEND", {});
    return png;
}

// A whole JPEG file (ImageIO doesn't read the size of one cut short): an EXIF orientation, then a
// 3 x 2 gray baseline image. Its one block is mid-gray: each Huffman table has one code, 1 bit long,
// for "no change" (DC) and "end of block" (AC).
std::vector<std::uint8_t> jpeg_file(int orientation) {
    std::vector<std::uint8_t> jpeg;
    append(jpeg, {0xFF, 0xD8});
    append(jpeg, {0xFF, 0xE1, 0, 34, 'E', 'x', 'i', 'f', 0, 0});
    append(jpeg, {'I', 'I', 42, 0, 8, 0, 0, 0}); // little-endian TIFF, its first IFD at 8
    append(jpeg, {1, 0, 0x12, 0x01, 3, 0, 1, 0, 0, 0, orientation, 0, 0, 0, 0, 0, 0, 0});
    append(jpeg, {0xFF, 0xDB, 0, 67, 0}); // quantization table 0: all 1
    jpeg.insert(jpeg.end(), 64, 1);
    append(jpeg, {0xFF, 0xC0, 0, 11, 8, 0, 2, 0, 3, 1, 1, 0x11, 0}); // 8-bit, 2 rows of 3, one component
    append(jpeg, {0xFF, 0xC4, 0, 38}); // Huffman tables DC 0 and AC 0: one code of length 1, for symbol 0
    for (const int table : {0x00, 0x10}) {
        append(jpeg, {table, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0});
    }
    append(jpeg, {0xFF, 0xDA, 0, 8, 1, 1, 0x00, 0, 63, 0}); // the scan: component 1, coefficients 0-63
    append(jpeg, {0x3F, 0xFF, 0xD9}); // the codes (bits 00, then 1s to the byte), the end of the image
    return jpeg;
}

} // namespace

TEST_CASE("images with too many pixels are refused before decoding", "[image]") {
    // 1 GB of pixels in 64 bytes: under stb's own limit (2^30 bytes), over Elegance's (2^27 pixels).
    const std::vector<std::uint8_t> huge = png_header(16000, 16000);
    CHECK_THROWS_WITH(detail::decode_image(huge), ContainsSubstring("pixels"));
}

TEST_CASE("decoded images are scaled down to the size asked for", "[image]") {
    const std::vector<std::uint8_t> pixels(std::size_t{64} * 32 * 4, 0x80);
    const auto png = detail::encode_png(pixels, 64, 32);
    REQUIRE(png);
    // NOLINTBEGIN(bugprone-unchecked-optional-access): REQUIRE(png) stops the test without one
    const auto image = detail::decode_image(*png, 16);
    CHECK(image->width == 16);
    CHECK(image->height == 8);
    CHECK(image->rgba.size() == std::size_t{16} * 8 * 4);
    CHECK(detail::decode_image(*png)->width == 64); // within the default size: as it is
    // NOLINTEND(bugprone-unchecked-optional-access)
}

TEST_CASE("a JPEG's EXIF orientation turns its upright size", "[image]") {
    const detail::ImageInfo turned = detail::probe_image(jpeg_file(6)); // turned right
    CHECK(turned.width == 2);
    CHECK(turned.height == 3);
    const detail::ImageInfo upright = detail::probe_image(jpeg_file(1));
    CHECK(upright.width == 3);
    CHECK(upright.height == 2);
}
