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

#include <array>
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

// Linux reads the EXIF orientation itself (from JPEG and PNG files); the other platforms' codecs
// are tested there.
#if defined(__linux__)

TEST_CASE("decoding turns a JPEG upright", "[image]") {
    const auto turned = detail::decode_image(jpeg_file(6));
    CHECK(turned->width == 2);
    CHECK(turned->height == 3);
    CHECK(turned->rgba.size() == std::size_t{2} * 3 * 4);
}

TEST_CASE("decoding turns a PNG upright by its eXIf chunk", "[image]") {
    // 2 x 2, stored as   red   green      (BGRA)
    //                    blue  white
    constexpr std::array<std::uint8_t, 16> kStored{0, 0, 255, 255, 0, 255, 0, 255, 255, 0, 0, 255, 255, 255, 255, 255};
    const auto png = detail::encode_png(kStored, 2, 2);
    REQUIRE(png);
    // The RGB of the decoded image's pixel (x, y).
    const auto rgb = [](const detail::DecodedImage& image, std::uint32_t x, std::uint32_t y) {
        const std::size_t at = (std::size_t{y} * image.width + x) * 4;
        return std::array<int, 3>{image.rgba[at], image.rgba[at + 1], image.rgba[at + 2]};
    };
    constexpr std::array<int, 3> kRed{255, 0, 0};
    constexpr std::array<int, 3> kGreen{0, 255, 0};
    constexpr std::array<int, 3> kBlue{0, 0, 255};
    constexpr std::array<int, 3> kWhite{255, 255, 255};

    const auto as_stored = detail::decode_image(*png); // NOLINT(bugprone-unchecked-optional-access): REQUIREd
    CHECK(rgb(*as_stored, 0, 0) == kRed);
    CHECK(rgb(*as_stored, 1, 0) == kGreen);

    // 6, turned right (90 degrees clockwise):   blue   red
    //                                           white  green
    const auto turned = detail::decode_image(with_png_orientation(*png, 6)); // NOLINT(bugprone-unchecked-optional-access)
    REQUIRE(turned->width == 2);
    REQUIRE(turned->height == 2);
    CHECK(rgb(*turned, 0, 0) == kBlue);
    CHECK(rgb(*turned, 1, 0) == kRed);
    CHECK(rgb(*turned, 0, 1) == kWhite);
    CHECK(rgb(*turned, 1, 1) == kGreen);

    // 2, mirrored:   green  red
    //                white  blue
    const auto mirrored = detail::decode_image(with_png_orientation(*png, 2)); // NOLINT(bugprone-unchecked-optional-access)
    CHECK(rgb(*mirrored, 0, 0) == kGreen);
    CHECK(rgb(*mirrored, 1, 1) == kBlue);
}

TEST_CASE("a PNG's eXIf orientation turns its upright size", "[image]") {
    const std::vector<std::uint8_t> png = black_png(3, 2);
    const detail::ImageInfo turned = detail::probe_image(with_png_orientation(png, 8)); // turned left
    CHECK(turned.width == 2);
    CHECK(turned.height == 3);
    // EXIF data that isn't whole, or isn't EXIF, changes nothing.
    std::vector<std::uint8_t> exif = exif_orientation(8);
    exif[4] = 200; // the IFD's offset, past the end
    CHECK(detail::probe_image(with_png_exif(png, exif)).width == 3);
    exif = exif_orientation(8);
    exif.resize(12); // the IFD's first entry cut short
    CHECK(detail::probe_image(with_png_exif(png, exif)).width == 3);
    CHECK(detail::probe_image(with_png_exif(png, {'M', 'M', 0, 42})).width == 3);
    CHECK(detail::probe_image(with_png_exif(png, {})).width == 3);
}

#endif
