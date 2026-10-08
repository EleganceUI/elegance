// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include "render/display_list.h"

#include <algorithm>
#include <cmath>

namespace elg::detail {

namespace {

template <class... Fs>
struct Overloaded : Fs... {
    using Fs::operator()...;
};

bool stops_clear(const std::vector<GradientStop>& stops) {
    return std::ranges::all_of(stops, [](const GradientStop& stop) { return stop.color.a <= 0; });
}

std::vector<GradientStop> fade(std::vector<GradientStop> stops, float opacity) {
    for (GradientStop& stop : stops) {
        stop.color = stop.color.opacity(opacity);
    }
    return stops;
}

} // namespace

bool is_clear(const ShapeStyle& style) {
    return std::visit(Overloaded{
                          [](const Color& color) { return color.a <= 0; },
                          [](const LinearGradient& gradient) { return stops_clear(gradient.stops); },
                          [](const RadialGradient& gradient) { return stops_clear(gradient.stops); },
                      },
                      style.value());
}

ShapeStyle with_opacity(const ShapeStyle& style, float opacity) {
    return std::visit(Overloaded{
                          [&](const Color& color) { return ShapeStyle(color.opacity(opacity)); },
                          [&](const LinearGradient& gradient) {
                              return ShapeStyle(LinearGradient(fade(gradient.stops, opacity), gradient.start,
                                                               gradient.end));
                          },
                          [&](const RadialGradient& gradient) {
                              return ShapeStyle(RadialGradient(fade(gradient.stops, opacity), gradient.center,
                                                               gradient.start_radius, gradient.end_radius));
                          },
                      },
                      style.value());
}

Rect union_of(const Rect& a, const Rect& b) {
    const float x = std::min(a.x, b.x);
    const float y = std::min(a.y, b.y);
    return {x, y, std::max(a.max_x(), b.max_x()) - x, std::max(a.max_y(), b.max_y()) - y};
}

std::optional<Rect> draw_bounds(const DrawCommand& command) {
    return std::visit(Overloaded{
                          [](const RectCommand& c) -> std::optional<Rect> { return c.rect; },
                          [](const GlyphCommand& c) -> std::optional<Rect> { return c.quad; },
                          [](const BoxShadowCommand& c) -> std::optional<Rect> {
                              // The Gaussian is negligible beyond three sigma.
                              return c.rect.inset(-1.5f * c.blur_radius - 1);
                          },
                          [](const PushLayerCommand& c) -> std::optional<Rect> {
                              if (!c.shadow) {
                                  return c.bounds;
                              }
                              const Rect shadow{c.bounds.x + c.shadow->x, c.bounds.y + c.shadow->y,
                                                c.bounds.width, c.bounds.height};
                              return union_of(c.bounds, shadow.inset(-1.5f * c.shadow->radius - 1));
                          },
                          [](const auto&) -> std::optional<Rect> { return std::nullopt; },
                      },
                      command);
}

} // namespace elg::detail
