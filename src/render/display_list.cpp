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
                          [](const EllipticalGradient& gradient) { return stops_clear(gradient.stops); },
                          [](const AngularGradient& gradient) { return stops_clear(gradient.stops); },
                          [](const BackdropFill&) { return false; },
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
                          [&](const EllipticalGradient& gradient) {
                              return ShapeStyle(EllipticalGradient(fade(gradient.stops, opacity), gradient.center,
                                                                   gradient.start_radius_fraction,
                                                                   gradient.end_radius_fraction));
                          },
                          [&](const AngularGradient& gradient) {
                              return ShapeStyle(AngularGradient(fade(gradient.stops, opacity), gradient.center,
                                                                gradient.start_angle, gradient.end_angle));
                          },
                          // What shows through a backdrop is opaque: it doesn't fade.
                          [&](const BackdropFill& backdrop) { return ShapeStyle(backdrop); },
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
                       [&](ImageCommand& c) { c.rect = rect(c.rect); },
                       [&](VideoCommand& c) { c.rect = rect(c.rect); },
                       [&](BoxShadowCommand& c) {
                           c.rect = rect(c.rect);
                           c.radii = radii(c.radii);
                           c.blur_radius *= scale;
                       },
                       [&](InnerShadowCommand& c) {
                           c.rect = rect(c.rect);
                           c.radii = radii(c.radii);
                           c.blur_radius *= scale;
                           c.offset = {c.offset.x * scale, c.offset.y * scale};
                           c.spread *= scale;
                       },
                       [&](PushClipCommand& c) {
                           c.rect = rect(c.rect);
                           c.radii = radii(c.radii);
                       },
                       [&](PushLayerCommand& c) {
                           c.bounds = rect(c.bounds);
                           if (c.mask) {
                               c.mask->offset = point(c.mask->offset.x, c.mask->offset.y);
                               c.mask->scale *= scale;
                               c.mask->paint_frame = rect(c.mask->paint_frame);
                           }
                           if (c.shadow) {
                               c.shadow->radius *= scale;
                               c.shadow->x *= scale;
                               c.shadow->y *= scale;
                           }
                           if (c.inner_shadow) {
                               c.inner_shadow->radius *= scale;
                               c.inner_shadow->x *= scale;
                               c.inner_shadow->y *= scale;
                           }
                           c.blur *= scale;
                           if (c.transform) {
                               // A uniform scale commutes with the layer's own rotation and scale.
                               c.transform->pivot = point(c.transform->pivot.x, c.transform->pivot.y);
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
        if (auto* video = std::get_if<VideoCommand>(&content.commands.front())) {
            video->opacity *= opacity;
            list.commands.push_back(std::move(*video));
            return;
        }
        if (auto* image = std::get_if<ImageCommand>(&content.commands.front())) {
            image->opacity *= opacity;
            list.commands.push_back(std::move(*image));
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
    list.commands.push_back(PushLayerCommand{*bounds, opacity, std::nullopt, std::nullopt});
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
                          [](const ImageCommand& c) -> std::optional<Rect> { return c.rect; },
                          [](const VideoCommand& c) -> std::optional<Rect> { return c.rect; },
                          [](const BoxShadowCommand& c) -> std::optional<Rect> {
                              // The Gaussian is negligible beyond three sigma.
                              return c.rect.inset(-1.5f * c.blur_radius - 1);
                          },
                          [](const InnerShadowCommand& c) -> std::optional<Rect> { return c.rect; },
                          [](const PushLayerCommand& c) -> std::optional<Rect> {
                              Rect bounds = c.bounds.inset(-1.5f * c.blur);
                              if (c.shadow) {
                                  const Rect shadow{c.bounds.x + c.shadow->x, c.bounds.y + c.shadow->y,
                                                    c.bounds.width, c.bounds.height};
                                  bounds = union_of(bounds, shadow.inset(-1.5f * c.shadow->radius - 1));
                              }
                              return c.transform ? transformed_bounds(*c.transform, bounds) : bounds;
                          },
                          [](const auto&) -> std::optional<Rect> { return std::nullopt; },
                      },
                      command);
}

// The filter functions of CSS (Filter Effects Level 1), as SVG feColorMatrix values; brightness is
// SwiftUI's (it adds), not CSS's (which multiplies).
ColorMatrix color_matrix(ColorFilter::Kind kind, float amount, Color color) {
    using Kind = ColorFilter::Kind;
    ColorMatrix m{1, 0, 0, 0, 0, //
                  0, 1, 0, 0, 0, //
                  0, 0, 1, 0, 0, //
                  0, 0, 0, 1, 0};
    const auto rgb = [&](std::array<float, 9> k) {
        for (std::size_t row = 0; row < 3; ++row) {
            for (std::size_t col = 0; col < 3; ++col) {
                m[row * 5 + col] = k[row * 3 + col];
            }
        }
    };
    const auto saturate = [&](float s) {
        rgb({0.213f + 0.787f * s, 0.715f - 0.715f * s, 0.072f - 0.072f * s, //
             0.213f - 0.213f * s, 0.715f + 0.285f * s, 0.072f - 0.072f * s, //
             0.213f - 0.213f * s, 0.715f - 0.715f * s, 0.072f + 0.928f * s});
    };
    switch (kind) {
    case Kind::brightness:
        m[4] = m[9] = m[14] = amount;
        break;
    case Kind::contrast:
        m[0] = m[6] = m[12] = amount;
        m[4] = m[9] = m[14] = 0.5f - 0.5f * amount;
        break;
    case Kind::saturation: saturate(std::max(amount, 0.f)); break;
    case Kind::grayscale: saturate(1 - std::clamp(amount, 0.f, 1.f)); break;
    case Kind::hue_rotation: {
        const float radians = amount * 3.14159265f / 180;
        const float c = std::cos(radians);
        const float s = std::sin(radians);
        rgb({0.213f + c * 0.787f - s * 0.213f, 0.715f - c * 0.715f - s * 0.715f, 0.072f - c * 0.072f + s * 0.928f,
             0.213f - c * 0.213f + s * 0.143f, 0.715f + c * 0.285f + s * 0.140f, 0.072f - c * 0.072f - s * 0.283f,
             0.213f - c * 0.213f - s * 0.787f, 0.715f - c * 0.715f + s * 0.715f, 0.072f + c * 0.928f + s * 0.072f});
        break;
    }
    case Kind::sepia: {
        const float k = 1 - std::clamp(amount, 0.f, 1.f);
        rgb({0.393f + 0.607f * k, 0.769f - 0.769f * k, 0.189f - 0.189f * k, //
             0.349f - 0.349f * k, 0.686f + 0.314f * k, 0.168f - 0.168f * k, //
             0.272f - 0.272f * k, 0.534f - 0.534f * k, 0.131f + 0.869f * k});
        break;
    }
    case Kind::invert: {
        const float a = std::clamp(amount, 0.f, 1.f);
        m[0] = m[6] = m[12] = 1 - 2 * a;
        m[4] = m[9] = m[14] = a;
        break;
    }
    case Kind::multiply:
        m[0] = color.r;
        m[6] = color.g;
        m[12] = color.b;
        m[18] = color.a;
        break;
    case Kind::luminance_to_alpha:
        m = {0, 0, 0, 0, 0, //
             0, 0, 0, 0, 0, //
             0, 0, 0, 0, 0, //
             0.2125f, 0.7154f, 0.0721f, 0, 0};
        break;
    }
    return m;
}

Color apply(const ColorMatrix& m, Color color) {
    const std::array<float, 4> in{color.r, color.g, color.b, color.a};
    std::array<float, 4> out{};
    for (std::size_t row = 0; row < 4; ++row) {
        float sum = m[row * 5 + 4];
        for (std::size_t col = 0; col < 4; ++col) {
            sum += m[row * 5 + col] * in[col];
        }
        out[row] = std::clamp(sum, 0.f, 1.f);
    }
    return {out[0], out[1], out[2], out[3]};
}

Rect transformed_bounds(const LayerTransform& t, const Rect& r) {
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    const Point corners[] = {{r.x, r.y}, {r.max_x(), r.y}, {r.x, r.max_y()}, {r.max_x(), r.max_y()}};
    for (std::size_t i = 0; i < 4; ++i) {
        const float dx = corners[i].x - t.pivot.x;
        const float dy = corners[i].y - t.pivot.y;
        const float x = t.pivot.x + t.a * dx + t.c * dy;
        const float y = t.pivot.y + t.b * dx + t.d * dy;
        x0 = i == 0 ? x : std::min(x0, x);
        y0 = i == 0 ? y : std::min(y0, y);
        x1 = i == 0 ? x : std::max(x1, x);
        y1 = i == 0 ? y : std::max(y1, y);
    }
    return {x0, y0, x1 - x0, y1 - y0};
}

} // namespace elg::detail
