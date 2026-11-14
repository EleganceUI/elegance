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
#include <utility>
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

// Packs bits into bytes as deflate does, from each byte's least significant bit.
class Bits {
public:
    // `count` bits of `value`, least significant first: one of deflate's numbers.
    void number(std::uint32_t value, int count) {
        for (int i = 0; i < count; ++i) {
            put((value >> i) & 1U);
        }
    }
    // A Huffman code of `length` bits, most significant first.
    void code(std::uint32_t value, int length) {
        for (int i = length - 1; i >= 0; --i) {
            put((value >> i) & 1U);
        }
    }
    // The bytes, the last one padded with zeros.
    std::vector<std::uint8_t> take() { return std::move(bytes_); }

private:
    void put(std::uint32_t bit) {
        if (used_ == 0) {
            bytes_.push_back(0);
        }
        bytes_.back() = static_cast<std::uint8_t>(bytes_.back() | (bit << used_));
        used_ = (used_ + 1) % 8;
    }

    std::vector<std::uint8_t> bytes_;
    int used_ = 0;
};

// A zlib stream of `count` zero bytes (at least one): one block of deflate's fixed Huffman codes, a
// literal 0, then copies of the 258 bytes before as long as they fit, then literals.
std::vector<std::uint8_t> zlib_zeros(std::size_t count) {
    Bits bits;
    bits.number(0x78, 8); // zlib's header: deflate, a 32 KB window
    bits.number(0x01, 8);
    bits.number(1, 1); // the last block
    bits.number(1, 2); // fixed codes
    bits.code(0x30, 8); // literal 0
    std::size_t left = count - 1;
    for (; left >= 258; left -= 258) {
        bits.code(0xC5, 8); // length 258 (symbol 285)
        bits.code(0, 5); // distance 1
    }
    for (; left > 0; --left) {
        bits.code(0x30, 8);
    }
    bits.code(0, 7); // the end of the block (symbol 256)
    std::vector<std::uint8_t> zlib = bits.take();
    // Adler-32: over zeros, the sum of the bytes stays 1 and the sum of those sums is the count.
    append_u32(zlib, static_cast<std::uint32_t>(((count % 65521) << 16) | 1));
    return zlib;
}

// A whole PNG file (ImageIO doesn't read the size of one cut short) of a `width` x `height` image,
// all black at one bit a pixel: its rows are all zero bytes, which compress to almost nothing.
std::vector<std::uint8_t> black_png(std::uint32_t width, std::uint32_t height) {
    std::vector<std::uint8_t> png;
    append(png, {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A});
    std::vector<std::uint8_t> header;
    append_u32(header, width);
    append_u32(header, height);
    append(header, {1, 0, 0, 0, 0}); // 1-bit grayscale, not interlaced
    append_chunk(png, "IHDR", header);
    const std::size_t row = 1 + ((std::size_t{width} + 7) / 8); // the filter (0, none), then the pixels
    append_chunk(png, "IDAT", zlib_zeros(row * height));
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
    // A row more than Elegance allows (2^27 pixels), in 106 KB; under stb's own limit (2^30 bytes).
    const std::vector<std::uint8_t> huge = black_png(16384, 8193);
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
