// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// What drawing puts on screen, checked by its pixels (ELGSPEC-0024). GPU tests: they skip where
// the GPU draws nothing (Dawn's Null backend).

#include "render_pixels.h"

#include "display/display_list.h"
#include "gpu/external_image.h"
#include "media/decoded_image.h"
#include "media/frame_source.h"
#include "media/video_frame.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <webgpu/webgpu_cpp.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <memory>
#include <numbers>
#include <optional>
#include <utility>
#include <vector>

using namespace elg;
using elg_tests::PixelRenderer;
using elg_tests::Pixels;

namespace {

std::shared_ptr<const detail::Polygons> rect_polygons(float width, float height) {
    return std::make_shared<const detail::Polygons>(
        detail::Polygons{{{0, 0}, {width, 0}, {width, height}, {0, height}}});
}

} // namespace

TEST_CASE("content 20000 pixels tall draws its last part when scrolled to its end", "[.gpu][large-content]") {
    PixelRenderer renderer;
    if (!renderer.draws()) {
        SKIP("the GPU here draws nothing");
    }
    detail::DisplayList list;
    list.clear_color = Color::white;

    SECTION("a path") {
        // Its last 50 pixels show, at the top of the window.
        list.commands.emplace_back(detail::PathCommand{rect_polygons(100, 20000), {0, 0, 100, 20000}, 1, {0, -19950},
                                                       {}, ShapeStyle(Color::black), FillRule::non_zero});
        const Pixels pixels = renderer.draw(list, 100, 100);
        CHECK(pixels.at(50, 10) == std::array{0, 0, 0, 255});
        CHECK(pixels.at(50, 45) == std::array{0, 0, 0, 255});
        CHECK(pixels.at(50, 60) == std::array{255, 255, 255, 255});
    }
    SECTION("a layer") {
        // Its content: a red rect at its end, which shows at the top of the window.
        list.commands.emplace_back(detail::PushLayerCommand{.bounds = {0, -19950, 100, 20000}});
        list.commands.emplace_back(
            detail::RectCommand{.rect = {0, -19950, 100, 20000}, .fill = ShapeStyle(Color::black)});
        list.commands.emplace_back(
            detail::RectCommand{.rect = {0, 0, 100, 50}, .fill = ShapeStyle(Color::rgb(255, 0, 0))});
        list.commands.emplace_back(detail::PopLayerCommand{});
        const Pixels pixels = renderer.draw(list, 100, 100);
        const std::array<int, 4> red = pixels.at(50, 25);
        CHECK(red[0] >= 250);
        CHECK(red[1] <= 5);
        CHECK(red[2] <= 5);
        CHECK(pixels.at(50, 75) == std::array{255, 255, 255, 255});
    }
}

namespace {

// An opaque white texel beside transparent red ones: filtered with straight alpha, the red tints
// the pixels between them.
std::vector<std::uint8_t> white_beside_clear_red(std::uint32_t width, std::uint32_t height, bool checkered) {
    std::vector<std::uint8_t> rgba(std::size_t{width} * height * 4);
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            const bool white = checkered ? (x + y) % 2 == 0 : x == 0;
            const std::size_t i = (std::size_t{y} * width + x) * 4;
            rgba[i] = 255;
            rgba[i + 1] = white ? 255 : 0;
            rgba[i + 2] = white ? 255 : 0;
            rgba[i + 3] = white ? 255 : 0;
        }
    }
    return rgba;
}

// Whether a pixel over black is gray: no channel tinted by a transparent texel's color.
bool gray(const std::array<int, 4>& pixel) {
    return std::abs(pixel[0] - pixel[1]) <= 2 && std::abs(pixel[1] - pixel[2]) <= 2;
}

std::shared_ptr<const detail::DecodedImage> image_of(std::uint32_t width, std::uint32_t height, bool checkered) {
    static std::uint64_t ids = 1000;
    return std::make_shared<const detail::DecodedImage>(
        detail::DecodedImage{++ids, width, height, white_beside_clear_red(width, height, checkered)});
}

// A GPU image of the test's, with straight alpha, as GpuImage imports them.
class TestImage final : public detail::gpu::ExternalImage {
public:
    TestImage(wgpu::Device device, wgpu::Texture texture)
        : device_(std::move(device)), texture_(std::move(texture)), view_(texture_.CreateView()) {}
    void accept() noexcept override {}
    [[nodiscard]] const wgpu::Device& device() const noexcept override { return device_; }
    [[nodiscard]] const wgpu::Texture& texture() const noexcept override { return texture_; }
    [[nodiscard]] const wgpu::TextureView& view() const noexcept override { return view_; }

private:
    wgpu::Device device_;
    wgpu::Texture texture_;
    wgpu::TextureView view_;
};

// The one frame of a GPU image.
class StillFrames final : public detail::FrameSource {
public:
    explicit StillFrames(detail::VideoFrame frame) : frame_(std::move(frame)) {}
    bool update() override { return false; }
    [[nodiscard]] std::optional<detail::VideoFrame> current() const override { return frame_; }

private:
    detail::VideoFrame frame_;
};

} // namespace

TEST_CASE("images are filtered with premultiplied alpha", "[.gpu][filtering]") {
    PixelRenderer renderer;
    if (!renderer.draws()) {
        SKIP("the GPU here draws nothing");
    }
    detail::DisplayList list;
    list.clear_color = Color::black;

    SECTION("an image drawn larger: between its texels") {
        list.commands.emplace_back(detail::ImageCommand{.rect = {0, 0, 100, 10}, .image = image_of(2, 1, false)});
        const Pixels pixels = renderer.draw(list, 100, 10);
        const std::array<int, 4> between = pixels.at(50, 5);
        CHECK(between[1] > 60); // partly white
        CHECK(between[1] < 240);
        for (int x = 0; x < 100; ++x) {
            INFO("x " << x);
            CHECK(gray(pixels.at(x, 5)));
        }
    }
    SECTION("an image drawn smaller: its mipmaps") {
        list.commands.emplace_back(detail::ImageCommand{.rect = {0, 0, 4, 4}, .image = image_of(64, 64, true)});
        const Pixels pixels = renderer.draw(list, 4, 4);
        const std::array<int, 4> average = pixels.at(2, 2);
        CHECK(average[1] > 60);
        CHECK(gray(average));
    }
    SECTION("a GPU image or video frame, with straight alpha") {
        wgpu::TextureDescriptor descriptor;
        descriptor.size = {2, 1, 1};
        descriptor.format = wgpu::TextureFormat::RGBA8UnormSrgb;
        descriptor.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
        const wgpu::Texture texture = renderer.device().CreateTexture(&descriptor);
        const std::vector<std::uint8_t> texels = white_beside_clear_red(2, 1, false);
        wgpu::TexelCopyTextureInfo destination;
        destination.texture = texture;
        wgpu::TexelCopyBufferLayout layout;
        layout.bytesPerRow = 8;
        layout.rowsPerImage = 1;
        const wgpu::Extent3D size{2, 1, 1};
        renderer.device().GetQueue().WriteTexture(&destination, texels.data(), texels.size(), &layout, &size);
        detail::VideoFrame frame;
        frame.kind = detail::VideoFrame::Kind::external;
        frame.width = 2;
        frame.height = 1;
        frame.texture_id = detail::next_media_id();
        frame.external = std::make_shared<TestImage>(renderer.device(), texture);
        list.commands.emplace_back(
            detail::VideoCommand{.rect = {0, 0, 100, 10}, .source = std::make_shared<StillFrames>(frame)});
        const Pixels pixels = renderer.draw(list, 100, 10);
        const std::array<int, 4> between = pixels.at(50, 5);
        CHECK(between[1] > 60);
        CHECK(between[1] < 240);
        for (int x = 0; x < 100; ++x) {
            INFO("x " << x);
            CHECK(gray(pixels.at(x, 5)));
        }
    }
}

namespace {

// The share of a Gaussian of `sigma` pixels that falls between `from` and `to`, from a pixel's
// center.
double gaussian_share(double center, double from, double to, double sigma) {
    const auto below = [&](double x) { return 0.5 * std::erfc(-(x - center) / (sigma * std::numbers::sqrt2)); };
    return below(to) - below(from);
}

int to_srgb_byte(double linear) {
    const double v = linear <= 0.0031308 ? linear * 12.92 : 1.055 * std::pow(linear, 1 / 2.4) - 0.055;
    return static_cast<int>(std::lround(std::clamp(v, 0.0, 1.0) * 255));
}

// How a black `rect` (pixels) blurred as a Gaussian of `sigma` pixels covers the pixel (x, y).
double blurred_cover(const Rect& rect, double sigma, int x, int y) {
    const auto d = [](float v) { return static_cast<double>(v); };
    return gaussian_share(x + 0.5, d(rect.x), d(rect.max_x()), sigma) *
           gaussian_share(y + 0.5, d(rect.y), d(rect.max_y()), sigma);
}

// The largest difference, in any channel, between `pixels` and what `expected` says (linear
// light, gray) of each pixel; where it is goes to the test's messages.
int largest_difference(const Pixels& pixels, const std::function<double(int, int)>& expected) {
    int largest = 0;
    int at_x = 0;
    int at_y = 0;
    int wanted = 0;
    int found = 0;
    for (int y = 0; y < pixels.height; ++y) {
        for (int x = 0; x < pixels.width; ++x) {
            const int want = to_srgb_byte(expected(x, y));
            const std::array<int, 4> got = pixels.at(x, y);
            for (int channel = 0; channel < 3; ++channel) {
                const int difference = std::abs(got[static_cast<std::size_t>(channel)] - want);
                if (difference > largest) {
                    largest = difference;
                    at_x = x;
                    at_y = y;
                    wanted = want;
                    found = got[static_cast<std::size_t>(channel)];
                }
            }
        }
    }
    UNSCOPED_INFO("largest difference at (" << at_x << ", " << at_y << "): " << found << " for " << wanted);
    return largest;
}

} // namespace

TEST_CASE("blurs and shadows are Gaussian blurs of half their radius times the scale", "[.gpu][gaussian]") {
    PixelRenderer renderer;
    if (!renderer.draws()) {
        SKIP("the GPU here draws nothing");
    }
    // Radius in points and the window's scale; sigma = radius * scale / 2 pixels. The widest run
    // at 1/32 the resolution.
    const auto [radius, scale] = GENERATE(table<float, float>({{3, 1}, {20, 1}, {60, 1}, {100, 2}, {200, 2}}));
    const double sigma = static_cast<double>(radius * scale) / 2;
    const auto margin = static_cast<float>(std::ceil(4 * sigma) + 8);
    const float side = 100;                        // the rect's, in points
    const auto size = static_cast<int>(std::ceil(side * scale + 2 * margin));
    const Rect rect{margin / scale, margin / scale, side, side};             // points
    const Rect pixels_rect{margin, margin, side * scale, side * scale};      // pixels
    INFO("radius " << radius << " at scale " << scale);

    detail::DisplayList list;
    list.clear_color = Color::white;
    SECTION("a blurred layer") {
        list.commands.emplace_back(detail::PushLayerCommand{.bounds = rect, .blur = radius});
        list.commands.emplace_back(detail::RectCommand{.rect = rect, .fill = ShapeStyle(Color::black)});
        list.commands.emplace_back(detail::PopLayerCommand{});
        const Pixels pixels = renderer.draw(list, size, size, scale);
        CHECK(largest_difference(pixels, [&](int x, int y) { return 1 - blurred_cover(pixels_rect, sigma, x, y); }) <=
              4);
    }
    SECTION("a layer's shadow") {
        const Shadow shadow{.color = Color::black, .radius = radius};
        list.commands.emplace_back(detail::PushLayerCommand{.bounds = rect, .shadow = shadow});
        list.commands.emplace_back(detail::RectCommand{.rect = rect, .fill = ShapeStyle(Color::black)});
        list.commands.emplace_back(detail::PopLayerCommand{});
        const Pixels pixels = renderer.draw(list, size, size, scale);
        CHECK(largest_difference(pixels, [&](int x, int y) {
                  const auto px = static_cast<float>(x);
                  const auto py = static_cast<float>(y);
                  const bool inside = px >= pixels_rect.x && px < pixels_rect.max_x() && py >= pixels_rect.y &&
                                      py < pixels_rect.max_y();
                  return inside ? 0 : 1 - blurred_cover(pixels_rect, sigma, x, y);
              }) <= 4);
    }
    SECTION("a backdrop blur") {
        list.commands.emplace_back(detail::RectCommand{.rect = rect, .fill = ShapeStyle(Color::black)});
        const float window = static_cast<float>(size) / scale;
        list.commands.emplace_back(
            detail::RectCommand{.rect = {0, 0, window, window}, .fill = ShapeStyle(BackdropFill{.blur = radius})});
        const Pixels pixels = renderer.draw(list, size, size, scale);
        CHECK(largest_difference(pixels, [&](int x, int y) { return 1 - blurred_cover(pixels_rect, sigma, x, y); }) <=
              4);
    }
    SECTION("an analytic box shadow") {
        list.commands.emplace_back(detail::BoxShadowCommand{.rect = rect, .color = Color::black, .blur_radius = radius});
        const Pixels pixels = renderer.draw(list, size, size, scale);
        CHECK(largest_difference(pixels, [&](int x, int y) { return 1 - blurred_cover(pixels_rect, sigma, x, y); }) <=
              4);
    }
}
