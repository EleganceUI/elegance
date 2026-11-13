// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Decoding image data, which comes from files and the network: size limits, scaling to the size
// asked for, and EXIF orientation.

#include "media/image_decode.h"

#include <elg/error.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cstdint>
#include <initializer_list>
#include <vector>

using namespace elg;
using Catch::Matchers::ContainsSubstring;

namespace {

void append(std::vector<std::uint8_t>& out, std::initializer_list<int> bytes) {
    for (const int byte : bytes) {
        out.push_back(static_cast<std::uint8_t>(byte));
    }
}

// The start of a PNG file: its signature, the header of a `width` x `height` RGBA image, and an
// empty start of its image data.
std::vector<std::uint8_t> png_header(std::uint32_t width, std::uint32_t height) {
    std::vector<std::uint8_t> png;
    append(png, {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A, 0, 0, 0, 13, 'I', 'H', 'D', 'R'});
    for (const std::uint32_t v : {width, height}) {
        append(png, {static_cast<int>(v >> 24), static_cast<int>((v >> 16) & 0xFF), static_cast<int>((v >> 8) & 0xFF),
                     static_cast<int>(v & 0xFF)});
    }
    append(png, {8, 6, 0, 0, 0, 0, 0, 0, 0}); // 8-bit RGBA, then a CRC
    append(png, {0, 0, 0, 0, 'I', 'D', 'A', 'T', 0, 0, 0, 0}); // stb reads up to the image data
    return png;
}

// The start of a JPEG file: an EXIF orientation, then the frame header of a 3 x 2 image.
std::vector<std::uint8_t> jpeg_header(int orientation) {
    std::vector<std::uint8_t> jpeg;
    append(jpeg, {0xFF, 0xD8});
    append(jpeg, {0xFF, 0xE1, 0, 34, 'E', 'x', 'i', 'f', 0, 0});
    append(jpeg, {'I', 'I', 42, 0, 8, 0, 0, 0}); // little-endian TIFF, its first IFD at 8
    append(jpeg, {1, 0, 0x12, 0x01, 3, 0, 1, 0, 0, 0, orientation, 0, 0, 0, 0, 0, 0, 0});
    append(jpeg, {0xFF, 0xC0, 0, 17, 8, 0, 2, 0, 3, 3, 1, 0x11, 0, 2, 0x11, 0, 3, 0x11, 0});
    return jpeg;
}

} // namespace

TEST_CASE("images with too many pixels are refused before decoding", "[image]") {
    // 1 GB of pixels in 45 bytes: under stb's own limit (2^30 bytes), over Elegance's (2^27 pixels).
    const std::vector<std::uint8_t> huge = png_header(16000, 16000);
    CHECK_THROWS_WITH(detail::decode_image(huge), ContainsSubstring("pixels"));
}

TEST_CASE("decoded images are scaled down to the size asked for", "[image]") {
    const std::vector<std::uint8_t> pixels(64 * 32 * 4, 0x80);
    const auto png = detail::encode_png(pixels, 64, 32);
    REQUIRE(png);
    const auto image = detail::decode_image(*png, 16);
    CHECK(image->width == 16);
    CHECK(image->height == 8);
    CHECK(image->rgba.size() == std::size_t{16} * 8 * 4);
    CHECK(detail::decode_image(*png)->width == 64); // within the default size: as it is
}

TEST_CASE("a JPEG's EXIF orientation turns its upright size", "[image]") {
    const detail::ImageInfo turned = detail::probe_image(jpeg_header(6)); // turned right
    CHECK(turned.width == 2);
    CHECK(turned.height == 3);
    const detail::ImageInfo upright = detail::probe_image(jpeg_header(1));
    CHECK(upright.width == 3);
    CHECK(upright.height == 2);
}
