// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Decoding image data, which comes from files and the network: size limits, scaling to the size
// asked for, and EXIF orientation.

#include "media/image_decode.h"

#include "core/log.h"
#include "core/ui_tasks.h"
#include "test_images.h"
#include "view/view_node.h"

#include <elg/error.h>
#include <elg/image.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <string>
#include <thread>
#include <variant>
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

TEST_CASE("an app's image limits replace the defaults, and 0 keeps them", "[image]") {
    const std::vector<std::uint8_t> huge = black_png(16384, 8193);
    // Raised: probed (decoding it would take 512 MiB).
    CHECK(detail::probe_image(huge, detail::image_limits(std::uint64_t{1} << 28, 0)).width == 16384);
    CHECK_THROWS_AS(detail::probe_image(huge, detail::image_limits(0, 0)), ResourceError);
    const detail::ImageLimits defaults = detail::image_limits(0, 0);
    CHECK(defaults.max_pixels == std::uint64_t{1} << 27);
    CHECK(defaults.max_side == 8192);
    // Lowered.
    CHECK_THROWS_WITH(detail::probe_image(black_png(64, 32), detail::image_limits(2047, 0)),
                      ContainsSubstring("more than the 2047"));
}

TEST_CASE("an Image's limits apply to it, and to no other", "[image]") {
    detail::Scope scope;
    const detail::Scope::Use use(scope);
    const std::vector<std::uint8_t> pixels(std::size_t{64} * 32 * 4, 0x80);
    const auto png = detail::encode_png(pixels, 64, 32);
    REQUIRE(png);
    const std::span<const unsigned char> data(*png); // NOLINT(bugprone-unchecked-optional-access): REQUIREd above

    // Over its pixel limit: refused (nothing to show, so no size), and logged.
    const std::unique_ptr<detail::Element> refused = detail::lower(Image({.data = data, .max_pixels = 2047}), {});
    CHECK(refused->size_that_fits({}).width == 0);
    CHECK(std::ranges::any_of(detail::recent_log_lines(), [](const std::string& line) {
        return line.contains("Image: image data: the image is 64 x 32 pixels, more than the 2047");
    }));
    // The same data with the default limit.
    const std::unique_ptr<detail::Element> shown = detail::lower(Image({.data = data}), {});
    CHECK(shown->size_that_fits({}).width == 64);

    // A lower longest side: decoded smaller, shown at the same size.
    const std::unique_ptr<detail::Element> small = detail::lower(Image({.data = data, .max_side = 16}), {});
    const Size size = small->size_that_fits({});
    CHECK(size.width == 64);
    CHECK(size.height == 32);
    small->place({}, {});
    std::shared_ptr<const detail::DecodedImage> drawn;
    for (int i = 0; i < 500 && !drawn; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10)); // decoded in the background
        detail::run_ui_tasks();
        detail::flush_effects();
        detail::DisplayList list;
        small->paint(list);
        for (const auto& command : list.commands) {
            if (const auto* image = std::get_if<detail::ImageCommand>(&command)) {
                drawn = image->image;
            }
        }
    }
    REQUIRE(drawn);
    CHECK(drawn->width == 16);
    CHECK(drawn->height == 8);
}

TEST_CASE("images decode no larger than the GPU's largest texture, whatever the limit asks", "[image]") {
    CHECK(detail::max_texture_side() >= 8192); // what every WebGPU device allows
    const std::vector<std::uint8_t> pixels(std::size_t{64} * 32 * 4, 0x80);
    const auto png = detail::encode_png(pixels, 64, 32);
    REQUIRE(png);
    const std::uint32_t device = detail::max_texture_side();
    detail::set_max_texture_side(16); // a device with small textures
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access): REQUIREd above
    const auto image = detail::decode_image(*png, detail::image_limits(0, 100000));
    detail::set_max_texture_side(device);
    CHECK(image->width == 16);
    CHECK(image->height == 8);
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

TEST_CASE("scaling down averages each share of the image, colors weighted by alpha", "[image]") {
    // 4 x 2: a red square, then a square with one blue pixel and three transparent green ones.
    std::vector<std::uint8_t> bgra;
    for (int y = 0; y < 2; ++y) {
        append(bgra, {0, 0, 255, 255, 0, 0, 255, 255}); // red, red
        append(bgra, y == 0 ? std::initializer_list<int>{255, 0, 0, 255, 0, 255, 0, 0} : std::initializer_list<int>{0, 255, 0, 0, 0, 255, 0, 0});
    }
    const auto png = detail::encode_png(bgra, 4, 2);
    REQUIRE(png);
    const auto image = detail::decode_image(*png, {.max_side = 2}); // NOLINT(bugprone-unchecked-optional-access)
    REQUIRE(image->width == 2);
    REQUIRE(image->height == 1);
    CHECK(std::vector<std::uint8_t>(image->rgba.begin(), image->rgba.begin() + 4) == std::vector<std::uint8_t>{255, 0, 0, 255});
    // Blue, a quarter opaque: the transparent pixels' green doesn't count.
    CHECK(std::vector<std::uint8_t>(image->rgba.begin() + 4, image->rgba.end()) == std::vector<std::uint8_t>{0, 0, 255, 64});

    // Sizes that don't divide: every output pixel gets a share, so a flat color stays flat.
    const std::vector<std::uint8_t> flat(std::size_t{1501} * 3 * 4, 0x40);
    const auto wide = detail::encode_png(flat, 1501, 3);
    REQUIRE(wide);
    const auto scaled = detail::decode_image(*wide, {.max_side = 1000}); // NOLINT(bugprone-unchecked-optional-access)
    CHECK(scaled->width == 1000);
    CHECK(scaled->height == 2);
    CHECK(std::ranges::all_of(scaled->rgba, [](std::uint8_t byte) { return byte == 0x40; }));
}

TEST_CASE("an image of no pixels is refused", "[image]") {
    // A BMP header for 3000 x 0 pixels, 24 bits each, and no pixel data.
    std::vector<std::uint8_t> bmp{'B', 'M'};
    const auto le32 = [&](std::uint32_t value) {
        for (const int shift : {0, 8, 16, 24}) {
            bmp.push_back(static_cast<std::uint8_t>(value >> shift));
        }
    };
    le32(54); // the file's size
    le32(0);
    le32(54); // where the pixels start
    le32(40); // BITMAPINFOHEADER
    le32(3000);
    le32(0);
    append(bmp, {1, 0, 24, 0}); // one plane, 24 bits
    for (int i = 0; i < 6; ++i) {
        le32(0);
    }
    CHECK_THROWS_AS(detail::probe_image(bmp), ResourceError);
    CHECK_THROWS_AS(detail::decode_image(bmp), ResourceError);
    CHECK_THROWS_AS(detail::decode_image(bmp, {.max_side = 1024}), ResourceError); // nothing to scale
}

TEST_CASE("a PNG whose first IDAT chunk is empty decodes", "[image]") {
    const std::vector<std::uint8_t> pixels(std::size_t{3} * 2 * 4, 0x80);
    const auto png = detail::encode_png(pixels, 3, 2);
    REQUIRE(png);
    constexpr std::size_t kAfterHeader = 8 + 25; // the signature and IHDR
    // NOLINTBEGIN(bugprone-unchecked-optional-access): REQUIREd above
    std::vector<std::uint8_t> split(png->begin(), png->begin() + kAfterHeader);
    append_chunk(split, "IDAT", {}); // stb reads its 0 bytes into a buffer it hasn't made yet
    split.insert(split.end(), png->begin() + kAfterHeader, png->end());
    // NOLINTEND(bugprone-unchecked-optional-access)
    const auto image = detail::decode_image(split);
    CHECK(image->width == 3);
    CHECK(image->rgba[0] == 0x80);
}

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
