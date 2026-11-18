// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Frames drawn over the last one, only where they changed, against the same frames drawn whole
// (ELGSPEC-0029.R2, R3, R7, R11). GPU tests: they skip where the GPU draws nothing.

#include "render_pixels.h"

#include "display/display_list.h"
#include "render/render_settings.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <array>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <variant>
#include <vector>

using namespace elg;
using elg_tests::PixelRenderer;
using elg_tests::Pixels;

namespace {

constexpr float kWidth = 200;  // points
constexpr float kHeight = 160; // points

// Sets a render setting for a scope.
class Setting {
public:
    Setting(std::atomic<bool>& setting, bool value) : setting_(setting), was_(setting.exchange(value)) {}
    ~Setting() { setting_ = was_; }
    Setting(const Setting&) = delete;
    Setting& operator=(const Setting&) = delete;
    Setting(Setting&&) = delete;
    Setting& operator=(Setting&&) = delete;

private:
    std::atomic<bool>& setting_;
    bool was_;
};

ShapeStyle fill(float r, float g, float b, float a = 1) {
    return ShapeStyle(Color{r, g, b, a});
}

// A window at `step`: from one step to the next, things move, change and come and go, each in
// a way of its own, while most of the window stays as it was.
detail::DisplayList scene(int step) {
    using namespace detail;
    const auto s = static_cast<float>(step);
    DisplayList list;
    list.clear_color = Color::white;
    std::vector<DrawCommand>& c = list.commands;

    // A gradient across the top, which never changes.
    c.emplace_back(RectCommand{.rect = {0, 0, kWidth, 50}, .fill = ShapeStyle(LinearGradient({Color{0.2f, 0.4f, 1},
                                                                                              Color{0.1f, 0.8f, 0.5f}}))});
    // A rounded rect sliding right by a pixel and a half: its anti-aliased edges cross pixels.
    c.emplace_back(RectCommand{.rect = {8 + 1.5f * s, 8, 34, 22}, .radii = CornerRadii::all(7), .fill = fill(1, 0.2f, 0.1f)});
    // A card with a shadow, in a layer, moving down over the gradient's edge.
    const Rect card{120, 18 + 2 * s, 50, 36};
    c.emplace_back(PushLayerCommand{.bounds = card, .shadow = Shadow{.color = Color{0, 0, 0, 0.5f}, .radius = 10, .y = 4}});
    c.emplace_back(RectCommand{.rect = card, .radii = CornerRadii::all(6), .fill = fill(0.95f, 0.95f, 1)});
    c.emplace_back(PopLayerCommand{});
    // A list scrolling in a rounded clip, an item of it there on some steps only.
    c.emplace_back(PushClipCommand{.rect = {8, 64, 80, 70}, .radii = CornerRadii::all(10)});
    for (int i = 0; i < 6; ++i) {
        if (i == 2 && step % 3 == 1) {
            continue;
        }
        const float shade = i % 2 == 0 ? 0.3f : 0.7f;
        c.emplace_back(RectCommand{.rect = {8, 64 + static_cast<float>(i) * 18 - 5 * s, 80, 16},
                                   .fill = fill(shade, shade, 0.9f)});
    }
    c.emplace_back(PopClipCommand{});
    // A box shadow whose blur changes.
    c.emplace_back(BoxShadowCommand{.rect = {104, 76, 36, 26}, .radii = CornerRadii::all(4), .color = Color::black,
                                    .blur_radius = step % 2 == 0 ? 4.f : 12.f});
    // Behind a frosted panel, a bar moving right: the panel draws what's behind it again.
    c.emplace_back(RectCommand{.rect = {150 + 3 * s, 96, 14, 50}, .fill = fill(0.6f, 0.1f, 0.8f)});
    c.emplace_back(RectCommand{.rect = {146, 108, 48, 30}, .radii = CornerRadii::all(5),
                               .fill = ShapeStyle(BackdropFill{.blur = 6, .brightness = 0.1f})});
    // A blurred layer that turns, multiplied with what's behind it.
    const float angle = 0.2f * s;
    c.emplace_back(PushLayerCommand{
        .bounds = {100, 120, 30, 20},
        .blur = 2,
        .blend = BlendMode::multiply,
        .transform = LayerTransform{std::cos(angle), std::sin(angle), -std::sin(angle), std::cos(angle), {115, 130}}});
    c.emplace_back(RectCommand{.rect = {100, 120, 30, 20}, .fill = fill(1, 0.8f, 0)});
    c.emplace_back(PopLayerCommand{});
    // A scroll view over the gradient, its content scrolled 3 points a step; a row in it changes
    // color on odd steps.
    c.emplace_back(PushScrollCommand{.id = 7, .viewport = {150, 2, 46, 44}, .origin = {150, 2 - 3 * s}});
    for (int row = 0; row < 10; ++row) {
        const float shade = row % 2 == 0 ? 0.9f : 0.5f;
        c.emplace_back(RectCommand{.rect = {0, 12 * static_cast<float>(row), 46, 12},
                                   .radii = CornerRadii::all(3),
                                   .fill = row == 4 && step % 2 == 1 ? fill(1, 0.5f, 0) : fill(shade, 0.8f, shade)});
    }
    c.emplace_back(PopScrollCommand{});
    // A triangle, a path, moving up by half a pixel.
    const auto triangle = std::make_shared<const Polygons>(Polygons{{{0, 20}, {10, 0}, {20, 20}}});
    const Point at{60, 136 - 0.5f * s};
    c.emplace_back(PathCommand{triangle, {0, 0, 20, 20}, 1, at, {at.x, at.y, 20, 20}, fill(0, 0.5f, 0)});
    return list;
}

// The largest difference of any channel of any pixel.
int largest_difference(const Pixels& a, const Pixels& b) {
    int largest = 0;
    for (int y = 0; y < a.height; ++y) {
        for (int x = 0; x < a.width; ++x) {
            const std::array<int, 4> p = a.at(x, y);
            const std::array<int, 4> q = b.at(x, y);
            for (std::size_t channel = 0; channel < 4; ++channel) {
                if (std::abs(p[channel] - q[channel]) > largest) {
                    largest = std::abs(p[channel] - q[channel]);
                    UNSCOPED_INFO("at (" << x << ", " << y << "): " << p[channel] << " for " << q[channel]);
                }
            }
        }
    }
    return largest;
}

} // namespace

TEST_CASE("frames drawn only where they changed match them drawn whole", "[.gpu][damage]") {
    PixelRenderer partial;
    if (!partial.draws()) {
        SKIP("the GPU here draws nothing");
    }
    PixelRenderer whole;
    const float scale = GENERATE(1.f, 1.5f);
    const auto width = static_cast<int>(std::ceil(kWidth * scale));
    const auto height = static_cast<int>(std::ceil(kHeight * scale));
    for (int step = 0; step < 6; ++step) {
        INFO("step " << step << " at scale " << scale);
        const detail::DisplayList list = scene(step);
        const Pixels drawn = partial.draw(list, width, height, scale);
        const Setting full(detail::render_settings().full_redraw, true);
        const Pixels expected = whole.draw(list, width, height, scale);
        CHECK(largest_difference(drawn, expected) <= 1);
    }
    CHECK(partial.frames_presented() == 6);
}

TEST_CASE("a frame in which nothing changed is neither drawn nor presented", "[.gpu][damage]") {
    PixelRenderer renderer;
    if (!renderer.draws()) {
        SKIP("the GPU here draws nothing");
    }
    detail::DisplayList list = scene(0);
    const Pixels first = renderer.draw(list, 200, 160);
    CHECK(renderer.frames_presented() == 1);
    // The same frame again, then one of its own that draws the same.
    CHECK(largest_difference(renderer.draw(list, 200, 160), first) == 0);
    CHECK(renderer.frames_presented() == 1);
    CHECK(largest_difference(renderer.draw(scene(0), 200, 160), first) == 0);
    CHECK(renderer.frames_presented() == 1);
    // A change wholly outside the window shows nowhere.
    list.commands.emplace_back(detail::RectCommand{.rect = {300, 20, 10, 10}, .fill = fill(1, 0, 0)});
    renderer.draw(list, 200, 160);
    CHECK(renderer.frames_presented() == 1);
    // One that shows is drawn.
    list.commands.emplace_back(detail::RectCommand{.rect = {20, 140, 10, 10}, .fill = fill(1, 0, 0)});
    CHECK(renderer.draw(list, 200, 160).at(25, 145) == std::array<int, 4>{255, 0, 0, 255});
    CHECK(renderer.frames_presented() == 2);
}

TEST_CASE("the damage tint shows each frame's damage, and nothing else", "[.gpu][damage]") {
    PixelRenderer renderer;
    if (!renderer.draws()) {
        SKIP("the GPU here draws nothing");
    }
    detail::DisplayList list;
    list.clear_color = Color::white;
    list.commands.emplace_back(detail::RectCommand{.rect = {10, 10, 20, 20}, .fill = fill(0, 0, 1)});
    const Setting tint(detail::render_settings().show_damage, true);
    // The first frame is damaged everywhere.
    const Pixels first = renderer.draw(list, 100, 100);
    CHECK(first.at(90, 90)[0] == 255); // white, magenta over it
    CHECK(first.at(90, 90)[1] < 255);

    // Only the rect that moved, where it was and where it is.
    std::get<detail::RectCommand>(list.commands[0]).rect.x = 50;
    const Pixels moved = renderer.draw(list, 100, 100);
    CHECK(moved.at(90, 90) == std::array<int, 4>{255, 255, 255, 255}); // not damaged: plain
    CHECK(moved.at(20, 20)[1] < 255);                                   // where it was: tinted
    CHECK(moved.at(20, 20)[0] > moved.at(20, 20)[1]);
    CHECK(moved.at(60, 20)[0] > 0); // where it is: blue, tinted
    CHECK(moved.at(5, 50) == std::array<int, 4>{255, 255, 255, 255});
}
