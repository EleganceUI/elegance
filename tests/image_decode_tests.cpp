// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Decoding image data, which comes from files and the network: size limits, scaling to the size
// asked for, and EXIF orientation.

#include "media/image_decode.h"

#include "test_images.h"

#include <elg/error.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

using namespace elg;
using namespace elg::test;
using Catch::Matchers::ContainsSubstring;

TEST_CASE("images with too many pixels are refused before decoding", "[image]") {
    // A row more than Elegance allows (2^27 pixels), in 106 KB; under stb's own limit (2^30 bytes).
    const std::vector<std::uint8_t> huge = black_png(16384, 8193);
    CHECK_THROWS_WITH(detail::decode_image(huge), ContainsSubstring("more than the 134217728"));
    // Already when only the size is read.
    CHECK_THROWS_AS(detail::probe_image(huge), ResourceError);
    CHECK_THROWS_WITH(detail::probe_image(huge), ContainsSubstring("16384 x 8193 pixels"));
}

TEST_CASE("decoded images are scaled down to the size asked for", "[image]") {
    const std::vector<std::uint8_t> pixels(std::size_t{64} * 32 * 4, 0x80);
    const auto png = detail::encode_png(pixels, 64, 32);
    REQUIRE(png);
    // NOLINTBEGIN(bugprone-unchecked-optional-access): REQUIRE(png) stops the test without one
    const auto image = detail::decode_image(*png, {.max_side = 16});
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
