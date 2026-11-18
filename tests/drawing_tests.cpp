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

#include <webgpu/webgpu_cpp.h>

#include <array>
#include <cstdint>
#include <cstdlib>
#include <memory>
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
