// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Drawing: shapes with fills, gradients and strokes, SVG paths, a shape computed from its
// frame, and immediate-mode drawing in a Canvas.

#include <elg/elegance.h>

#include <array>
#include <cmath>
#include <numbers>

using namespace elg;

View Shapes() {
    return HStack({.spacing = 20},
                  Circle().fill(LinearGradient({Color::hex(0xF7768E), Color::hex(0xFF9E64)})),
                  RoundedRectangle(14).stroke(ThemeColor::accent, {.width = 3, .dash = {8, 5}}),
                  Capsule().fill(RadialGradient({Color::hex(0x9ECE6A), Color::hex(0x2AC3DE)}, UnitPoint::center, 0, 40)),
                  // An SVG path, scaled from its 24×24 view box to the frame.
                  PathShape(Path::svg("M12 21s-7.5-4.6-9.6-9.3C.9 8.4 3.2 4.5 6.9 4.5c2.1 0 3.6 1.2 5.1 3 "
                                      "1.5-1.8 3-3 5.1-3 3.7 0 6 3.9 4.5 7.2C19.5 16.4 12 21 12 21z"),
                            {.view_box = Rect{0, 0, 24, 24}})
                      .fill(Color::hex(0xBB9AF7)))
        .frame({.height = 72});
}

// A shape whose path depends on its size: a sine wave across the frame.
View Wave() {
    return CustomShape([](Rect r) {
               Path path;
               path.move_to({0, r.height / 2});
               for (float x = 0; x <= r.width; x += 2) {
                   const float phase = x / r.width * 4 * std::numbers::pi_v<float>;
                   path.line_to({x, r.height / 2 - std::sin(phase) * r.height * 0.4f});
               }
               return path;
           })
        .stroke(ThemeColor::accent, {.width = 3, .cap = LineCap::round, .join = LineJoin::round})
        .frame({.height = 60});
}

// Immediate mode: the closure runs whenever the canvas is drawn.
View Chart() {
    static constexpr std::array<float, 7> values{0.35f, 0.6f, 0.45f, 0.8f, 0.55f, 0.9f, 0.7f};
    return Canvas([](GraphicsContext& g) {
               const Size size = g.size();
               const float slot = size.width / static_cast<float>(values.size());
               Path trend;
               for (std::size_t i = 0; i < values.size(); ++i) {
                   const float height = values[i] * size.height;
                   const Rect bar{static_cast<float>(i) * slot + slot * 0.2f, size.height - height, slot * 0.6f, height};
                   g.fill_rect(bar, Color::hex(0x7AA2F7).opacity(0.35f), 6);
                   const Point top{bar.center().x, bar.y};
                   i == 0 ? trend.move_to(top) : trend.line_to(top);
               }
               g.stroke_path(trend, Color::hex(0x7AA2F7), {.width = 2.5f, .cap = LineCap::round, .join = LineJoin::round});
           })
        .frame({.height = 140});
}

View Gallery() {
    return VStack({.alignment = HorizontalAlignment::leading, .spacing = 16},
                  Text("Shapes").font(Font::headline), Shapes(),
                  Text("CustomShape").font(Font::headline), Wave(),
                  Text("Canvas").font(Font::headline), Chart())
        .padding(24);
}

int main() {
    App app;
    app.window({.title = "Drawing", .width = 560, .height = 520}, Gallery);
    return app.run();
}
