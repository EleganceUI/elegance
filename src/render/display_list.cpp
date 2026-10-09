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

void transform(std::vector<DrawCommand>& commands, float dx, float dy, float scale, Point origin) {
    if (dx == 0 && dy == 0 && scale == 1) {
        return;
    }
    const auto point = [&](float x, float y) {
        return Point{origin.x + (x - origin.x) * scale + dx, origin.y + (y - origin.y) * scale + dy};
    };
    const auto rect = [&](const Rect& r) {
        const Point p = point(r.x, r.y);
        return Rect{p.x, p.y, r.width * scale, r.height * scale};
    };
    const auto radii = [&](const CornerRadii& r) {
        return CornerRadii{r.top_left * scale, r.top_right * scale, r.bottom_right * scale, r.bottom_left * scale};
    };
    for (DrawCommand& command : commands) {
        std::visit(Overloaded{
                       [&](RectCommand& c) {
                           c.rect = rect(c.rect);
                           c.radii = radii(c.radii);
                           c.border_width *= scale;
                       },
                       [&](GlyphCommand& c) {
                           c.quad = rect(c.quad);
                           c.em_size *= scale;
                       },
                       [&](PathCommand& c) {
                           c.offset = point(c.offset.x, c.offset.y);
                           c.scale *= scale;
                           c.paint_frame = rect(c.paint_frame);
                       },
                       [&](BoxShadowCommand& c) {
                           c.rect = rect(c.rect);
                           c.radii = radii(c.radii);
                           c.blur_radius *= scale;
                       },
                       [&](PushClipCommand& c) {
                           c.rect = rect(c.rect);
                           c.radii = radii(c.radii);
                       },
                       [&](PushLayerCommand& c) {
                           c.bounds = rect(c.bounds);
                           if (c.shadow) {
                               c.shadow->radius *= scale;
                               c.shadow->x *= scale;
                               c.shadow->y *= scale;
                           }
                       },
                       [](auto&) {},
                   },
                   command);
    }
}

void append_faded(DisplayList& list, DisplayList&& content, float opacity) {
    if (opacity >= 1) {
        list.commands.insert(list.commands.end(), std::make_move_iterator(content.commands.begin()),
                             std::make_move_iterator(content.commands.end()));
        return;
    }
    if (opacity <= 0 || content.commands.empty()) {
        return;
    }
    if (content.commands.size() == 1) {
        if (auto* rect = std::get_if<RectCommand>(&content.commands.front())) {
            rect->fill = with_opacity(rect->fill, opacity);
            rect->border = rect->border.opacity(opacity);
            list.commands.push_back(std::move(*rect));
            return;
        }
        if (auto* path = std::get_if<PathCommand>(&content.commands.front())) {
            path->fill = with_opacity(path->fill, opacity);
            list.commands.push_back(std::move(*path));
            return;
        }
    }
    std::optional<Rect> bounds;
    for (const DrawCommand& command : content.commands) {
        if (const std::optional<Rect> b = draw_bounds(command)) {
            bounds = bounds ? union_of(*bounds, *b) : *b;
        }
    }
    if (!bounds) {
        return;
    }
    list.commands.push_back(PushLayerCommand{*bounds, opacity, std::nullopt});
    list.commands.insert(list.commands.end(), std::make_move_iterator(content.commands.begin()),
                         std::make_move_iterator(content.commands.end()));
    list.commands.push_back(PopLayerCommand{});
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
                          [](const PathCommand& c) -> std::optional<Rect> { return c.window_bounds(); },
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
