// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_TESTS_RENDER_PIXELS_H
#define ELG_TESTS_RENDER_PIXELS_H

// For GPU tests that check pixels: draws display lists as the render thread draws a window's
// frames, into an offscreen image, and reads its pixels back.

#include "display/display_list.h"

#include <webgpu/webgpu_cpp.h>

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace elg_tests {

// A frame's pixels: sRGB, 8 bits a channel, with the alpha the window would get, top row first.
struct Pixels {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;

    [[nodiscard]] std::array<int, 4> at(int x, int y) const;
};

class PixelRenderer {
public:
    PixelRenderer();
    ~PixelRenderer();
    PixelRenderer(const PixelRenderer&) = delete;
    PixelRenderer& operator=(const PixelRenderer&) = delete;
    PixelRenderer(PixelRenderer&&) = delete;
    PixelRenderer& operator=(PixelRenderer&&) = delete;

    // Whether its GPU draws: Dawn's Null backend (no GPU here) draws nothing.
    [[nodiscard]] bool draws() const;
    // The device it draws with, for textures of the test's.
    [[nodiscard]] const wgpu::Device& device() const;

    // Draws `list` into a `width` x `height` image, at `scale` pixels a point, and reads it back.
    // Each draws over the last one, as a window's frames do: only what changed is drawn again.
    Pixels draw(const elg::detail::DisplayList& list, int width, int height, float scale = 1);
    // How many of those frames were drawn and presented: not those in which nothing changed.
    [[nodiscard]] int frames_presented() const;
    // The parts of the image the last frame presented drew (pixels), as its presenter got them.
    [[nodiscard]] std::vector<elg::Rect> presented_area() const;
    // How many tiles of scroll views it drew: not those it kept.
    [[nodiscard]] int tiles_drawn() const;

private:
    struct Objects;
    std::unique_ptr<Objects> objects_;
};

} // namespace elg_tests

#endif // ELG_TESTS_RENDER_PIXELS_H
