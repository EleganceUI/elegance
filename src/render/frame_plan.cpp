// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include "render/frame_plan.h"

#include "core/log.h"
#include "render/color_space.h"
#include "text/atlas.h"

#include <algorithm>
#include <cmath>

namespace elg::detail {

namespace {

using gpu::PaintKind;
using gpu::QuadInstance;
using gpu::QuadKind;

template <class... Fs>
struct Overloaded : Fs... {
    using Fs::operator()...;
};

constexpr float kUnclipped = 1e7f;
constexpr std::uint32_t kMaxLayerSize = 8192;

struct Clip {
    Rect rect{-kUnclipped, -kUnclipped, 2 * kUnclipped, 2 * kUnclipped};
    CornerRadii radii;
    bool active = false;
};

Rect intersect(const Rect& a, const Rect& b) {
    const float x0 = std::max(a.x, b.x);
    const float y0 = std::max(a.y, b.y);
    const float x1 = std::min(a.max_x(), b.max_x());
    const float y1 = std::min(a.max_y(), b.max_y());
    return {x0, y0, std::max(0.f, x1 - x0), std::max(0.f, y1 - y0)};
}

bool contains(const Rect& outer, const Rect& inner) {
    return inner.x >= outer.x && inner.y >= outer.y && inner.max_x() <= outer.max_x() &&
           inner.max_y() <= outer.max_y();
}

bool overlaps(const Rect& a, const Rect& b) {
    return a.x < b.max_x() && b.x < a.max_x() && a.y < b.max_y() && b.y < a.max_y();
}

std::array<float, 4> scaled_radii(const CornerRadii& radii, float scale, float width, float height) {
    const float max_radius = 0.5f * std::min(width, height);
    const auto r = [&](float value) { return std::clamp(value * scale, 0.f, max_radius); };
    return {r(radii.top_left), r(radii.top_right), r(radii.bottom_right), r(radii.bottom_left)};
}

class Planner {
public:
    Planner(const DisplayList& list, FramePlan& plan) : list_(list), plan_(plan), scale_(list.scale) {}

    void run() {
        std::vector<QuadInstance> main_instances;
        std::size_t index = 0;
        build(index, {0, 0}, plan_.main, main_instances);

        // One instance buffer: layers first, in render order, then the main pass.
        for (std::size_t i = 0; i < plan_.layers.size(); ++i) {
            append(plan_.layers[i], layer_instances_[i]);
        }
        append(plan_.main, main_instances);
        if (plan_.stops.empty()) {
            plan_.stops.push_back({0, 0, 0, 0}); // the stops buffer must never be empty
        }
    }

private:
    void append(PlanPass& pass, const std::vector<QuadInstance>& instances) {
        const auto base = static_cast<std::uint32_t>(plan_.instances.size());
        plan_.instances.insert(plan_.instances.end(), instances.begin(), instances.end());
        for (PlanSegment& segment : pass.segments) {
            segment.first += base;
        }
    }

    // Converts a rect in window points to pixels relative to the current pass's origin.
    Rect to_pixels(const Rect& r, Point origin) const {
        return {r.x * scale_ - origin.x, r.y * scale_ - origin.y, r.width * scale_, r.height * scale_};
    }

    void emit(PlanPass& pass, std::vector<QuadInstance>& out, QuadInstance instance, const Rect& extent,
              const Clip& clip, int layer = -1, bool shadow = false, int path = -1, bool mask = false,
              int image = -1) {
        if (clip.active && !overlaps(extent, clip.rect)) {
            return; // fully clipped
        }
        instance.clip_rect = {clip.rect.x, clip.rect.y, clip.rect.width, clip.rect.height};
        instance.clip_radii = {clip.radii.top_left, clip.radii.top_right, clip.radii.bottom_right,
                               clip.radii.bottom_left};
        const auto index = static_cast<std::uint32_t>(out.size());
        out.push_back(instance);

        PlanSegment* last = pass.segments.empty() ? nullptr : &pass.segments.back();
        const bool plain = layer < 0 && path < 0 && image < 0;
        if (last && last->first + last->count == index &&
            ((plain && last->layer < 0 && last->path < 0 && last->image < 0) || (image >= 0 && last->image == image))) {
            ++last->count;
        } else {
            pass.segments.push_back({index, 1, layer, shadow, path, mask, image});
        }
    }

    // Records gradient stops; returns the first index and count.
    std::pair<std::uint32_t, std::uint32_t> add_stops(std::vector<GradientStop> stops) {
        std::ranges::stable_sort(stops, {}, &GradientStop::location);
        const auto first = static_cast<std::uint32_t>(plan_.stops.size() / 2);
        for (const GradientStop& stop : stops) {
            plan_.stops.push_back({stop.color.r, stop.color.g, stop.color.b, std::clamp(stop.color.a, 0.f, 1.f)});
            plan_.stops.push_back({stop.location, 0, 0, 0});
        }
        return {first, static_cast<std::uint32_t>(stops.size())};
    }

    // The fill: a color, or a gradient whose unit points span `r` (pixels).
    void set_paint(QuadInstance& instance, const ShapeStyle& fill, const Rect& r) {
        std::visit(Overloaded{
                       [&](const Color& color) { instance.color = to_linear_premultiplied(color); },
                       [&](const LinearGradient& gradient) {
                           if (gradient.stops.empty()) {
                               return;
                           }
                           const auto [first, count] = add_stops(gradient.stops);
                           instance.params[2] = static_cast<float>(PaintKind::linear);
                           instance.params[3] = static_cast<float>(first);
                           instance.extra[0] = static_cast<float>(count);
                           instance.gradient = {r.x + gradient.start.x * r.width, r.y + gradient.start.y * r.height,
                                                r.x + gradient.end.x * r.width, r.y + gradient.end.y * r.height};
                       },
                       [&](const RadialGradient& gradient) {
                           if (gradient.stops.empty()) {
                               return;
                           }
                           const auto [first, count] = add_stops(gradient.stops);
                           instance.params[2] = static_cast<float>(PaintKind::radial);
                           instance.params[3] = static_cast<float>(first);
                           instance.extra[0] = static_cast<float>(count);
                           instance.gradient = {r.x + gradient.center.x * r.width,
                                                r.y + gradient.center.y * r.height,
                                                gradient.start_radius * scale_, gradient.end_radius * scale_};
                       },
                   },
                   fill.value());
    }

    void add_rect(const RectCommand& command, Point origin, PlanPass& pass, std::vector<QuadInstance>& out,
                  const Clip& clip) {
        const Rect r = to_pixels(command.rect, origin);
        QuadInstance instance;
        instance.rect = {r.x, r.y, r.width, r.height};
        instance.shape = scaled_radii(command.radii, scale_, r.width, r.height);
        instance.border_color = to_linear_premultiplied(command.border);
        instance.params = {command.border_width * scale_, static_cast<float>(QuadKind::rect),
                           static_cast<float>(PaintKind::solid), 0};
        set_paint(instance, command.fill, r);
        emit(pass, out, instance, r.inset(-1), clip);
    }

    // A path: its fan triangles go to a coverage mask; the pass fills through the mask. With
    // `mask_pass` (a pass's size in pixels), the path instead masks what the pass has drawn so far:
    // a quad over the whole pass keeps only what the coverage covers.
    void add_path(const PathCommand& command, Point origin, PlanPass& pass, std::vector<QuadInstance>& out,
                  const Clip& clip, std::optional<Size> mask_pass = std::nullopt) {
        if (!command.polygons || command.polygons->empty()) {
            return;
        }
        const Rect bounds = to_pixels(command.window_bounds(), origin);
        const float x0 = std::floor(bounds.x) - 1;
        const float y0 = std::floor(bounds.y) - 1;
        const float x1 = std::ceil(bounds.max_x()) + 1;
        const float y1 = std::ceil(bounds.max_y()) + 1;
        const Rect area{x0, y0, std::min(x1 - x0, static_cast<float>(kMaxLayerSize)),
                        std::min(y1 - y0, static_cast<float>(kMaxLayerSize))};
        if (!mask_pass && (area.width < 1 || area.height < 1 || (clip.active && !overlaps(area, clip.rect)))) {
            return;
        }

        PlanPath path;
        path.width = static_cast<std::uint32_t>(area.width);
        path.height = static_cast<std::uint32_t>(area.height);
        path.first_vertex = static_cast<std::uint32_t>(plan_.path_vertices.size());
        path.even_odd = command.rule == FillRule::even_odd;
        const float k = command.scale * scale_;
        // Sub-pixel positions snap to a quarter pixel (at most 1/8 px off, invisible), so a moving
        // path (scrolling, animations) cycles through a few cached masks instead of a new one
        // every frame.
        const auto snap = [](float v) { return std::floor(v) + std::round((v - std::floor(v)) * 4) / 4; };
        const float dx = snap(command.offset.x * scale_ - origin.x - x0);
        const float dy = snap(command.offset.y * scale_ - origin.y - y0);
        const auto vertex = [&](Point p) { return std::array<float, 2>{p.x * k + dx, p.y * k + dy}; };

        // The content key: the polygons, then everything that changes how they rasterize. The
        // integer part of the position doesn't (the mask moves with the quad), the fraction does.
        std::uint64_t key = 14695981039346656037ull;
        const auto mix = [&](const void* data, std::size_t size) {
            const auto* bytes = static_cast<const unsigned char*>(data);
            for (std::size_t i = 0; i < size; ++i) {
                key = (key ^ bytes[i]) * 1099511628211ull;
            }
        };
        for (const std::vector<Point>& polygon : *command.polygons) {
            mix(polygon.data(), polygon.size() * sizeof(Point));
            const std::size_t count = polygon.size();
            mix(&count, sizeof(count));
        }
        const float subpixel_x = dx - std::floor(dx);
        const float subpixel_y = dy - std::floor(dy);
        const std::array<float, 3> placement{k, subpixel_x, subpixel_y};
        mix(placement.data(), sizeof(placement));
        const std::array<std::uint32_t, 3> shape{static_cast<std::uint32_t>(area.width),
                                                  static_cast<std::uint32_t>(area.height),
                                                  command.rule == FillRule::even_odd ? 1u : 0u};
        mix(shape.data(), sizeof(shape));
        path.key = key;
        for (const std::vector<Point>& polygon : *command.polygons) {
            // A fan from the first point: overlapping triangles cancel or add up in the stencil
            // exactly as the polygon winds.
            for (std::size_t i = 1; i + 1 < polygon.size(); ++i) {
                plan_.path_vertices.push_back(vertex(polygon[0]));
                plan_.path_vertices.push_back(vertex(polygon[i]));
                plan_.path_vertices.push_back(vertex(polygon[i + 1]));
            }
        }
        path.vertex_count = static_cast<std::uint32_t>(plan_.path_vertices.size()) - path.first_vertex;
        if (mask_pass && (path.vertex_count == 0 || area.width < 1 || area.height < 1)) {
            // Nothing is covered: clear the whole pass.
            QuadInstance clear;
            clear.rect = {0, 0, mask_pass->width, mask_pass->height};
            clear.params = {0, static_cast<float>(QuadKind::rect), static_cast<float>(PaintKind::solid), 0};
            emit(pass, out, clear, clear_extent(*mask_pass), Clip{}, -1, false, -1, true);
            return;
        }
        if (path.vertex_count == 0) {
            return;
        }
        const int index = static_cast<int>(plan_.paths.size());
        plan_.paths.push_back(path);

        if (mask_pass) {
            // Over the whole pass; outside the mask's own area the sampler clamps to its empty
            // border.
            QuadInstance instance;
            instance.rect = {0, 0, mask_pass->width, mask_pass->height};
            instance.shape = {-area.x, -area.y, mask_pass->width - area.x, mask_pass->height - area.y};
            instance.color = {1, 1, 1, 1};
            instance.params = {0, static_cast<float>(QuadKind::path), static_cast<float>(PaintKind::solid), 0};
            emit(pass, out, instance, clear_extent(*mask_pass), Clip{}, -1, false, index, true);
            return;
        }

        QuadInstance instance;
        instance.rect = {area.x, area.y, area.width, area.height};
        instance.shape = {0, 0, area.width, area.height};
        instance.params = {0, static_cast<float>(QuadKind::path), static_cast<float>(PaintKind::solid), 0};
        set_paint(instance, command.fill, to_pixels(command.paint_frame, origin));
        emit(pass, out, instance, area, clip, -1, false, index);
    }

    static Rect clear_extent(Size size) { return {0, 0, size.width, size.height}; }

    void add_image(const ImageCommand& command, Point origin, PlanPass& pass, std::vector<QuadInstance>& out,
                   const Clip& clip) {
        if (!command.image || command.image->width == 0 || command.opacity <= 0) {
            return;
        }
        const Rect r = to_pixels(command.rect, origin);
        if (r.width <= 0 || r.height <= 0) {
            return;
        }
        // One entry per image per frame, however often it's drawn.
        int index = -1;
        for (std::size_t i = 0; i < plan_.images.size(); ++i) {
            if (plan_.images[i]->id == command.image->id) {
                index = static_cast<int>(i);
                break;
            }
        }
        if (index < 0) {
            index = static_cast<int>(plan_.images.size());
            plan_.images.push_back(command.image);
        }
        QuadInstance instance;
        instance.rect = {r.x, r.y, r.width, r.height};
        instance.shape = {command.uv.x, command.uv.y, command.uv.max_x(), command.uv.max_y()};
        instance.color = {0, 0, 0, std::clamp(command.opacity, 0.f, 1.f)};
        instance.params = {0, static_cast<float>(QuadKind::image), 0, 0};
        emit(pass, out, instance, r, clip, -1, false, -1, false, index);
    }

    void add_glyph(const GlyphCommand& command, Point origin, PlanPass& pass, std::vector<QuadInstance>& out,
                   const Clip& clip) {
        const Rect q = to_pixels(command.quad, origin);
        // The atlas holds the field at kGlyphEmPixels; scale its range to the on-screen size.
        const float screen_range = kDistanceRange * command.em_size * scale_ / kGlyphEmPixels;
        QuadInstance instance;
        instance.rect = {q.x, q.y, q.width, q.height};
        instance.shape = {command.uv.x, command.uv.y, command.uv.x + command.uv.width, command.uv.y + command.uv.height};
        instance.color = to_linear_premultiplied(command.color);
        instance.params = {screen_range, static_cast<float>(QuadKind::glyph), 0, 0};
        emit(pass, out, instance, q, clip);
    }

    void add_box_shadow(const BoxShadowCommand& command, Point origin, PlanPass& pass,
                        std::vector<QuadInstance>& out, const Clip& clip) {
        const Rect r = to_pixels(command.rect, origin);
        const float sigma = std::max(0.5f, command.blur_radius * scale_ / 2);
        QuadInstance instance;
        instance.rect = {r.x, r.y, r.width, r.height};
        instance.shape = scaled_radii(command.radii, scale_, r.width, r.height);
        instance.color = to_linear_premultiplied(command.color);
        instance.params = {0, static_cast<float>(QuadKind::box_shadow), 0, 0};
        instance.extra = {0, sigma, 0, 0};
        emit(pass, out, instance, r.inset(-3 * sigma - 1), clip);
    }

    Clip push_clip(const PushClipCommand& command, Point origin, const Clip& current) const {
        const Rect r = to_pixels(command.rect, origin);
        const std::array<float, 4> radii = scaled_radii(command.radii, scale_, r.width, r.height);
        Clip clip;
        clip.active = true;
        clip.rect = intersect(current.rect, r);
        // Nested clips intersect exactly for rects; for rounded rects this keeps the inner
        // clip's corners (or the outer's when the inner clip is larger), an approximation.
        if (current.active && contains(r, current.rect)) {
            clip.radii = current.radii;
        } else {
            clip.radii = {radii[0], radii[1], radii[2], radii[3]};
        }
        return clip;
    }

    void add_layer(const PushLayerCommand& command, std::size_t& index, Point origin, PlanPass& pass,
                   std::vector<QuadInstance>& out, const Clip& clip) {
        const float sigma = command.shadow ? std::max(0.5f, command.shadow->radius * scale_ / 2) : 0.f;
        // Room around the content for anti-aliasing and the blur.
        const float margin = command.shadow ? std::ceil(3 * sigma) + 2 : 1;
        const Rect content = to_pixels(command.bounds, origin);
        const float x0 = std::floor(content.x - margin);
        const float y0 = std::floor(content.y - margin);
        const float x1 = std::ceil(content.max_x() + margin);
        const float y1 = std::ceil(content.max_y() + margin);
        const auto clamp_size = [](float v) {
            return static_cast<std::uint32_t>(std::clamp(v, 1.f, static_cast<float>(kMaxLayerSize)));
        };
        PlanPass layer;
        layer.width = clamp_size(x1 - x0);
        layer.height = clamp_size(y1 - y0);
        if (command.shadow) {
            layer.shadow_sigma = sigma;
        }

        std::vector<QuadInstance> layer_out;
        ++index;
        const Point layer_origin{origin.x + x0, origin.y + y0};
        build(index, layer_origin, layer, layer_out);
        if (command.mask) {
            add_path(*command.mask, layer_origin, layer, layer_out, Clip{},
                     Size{static_cast<float>(layer.width), static_cast<float>(layer.height)});
        }

        const int layer_index = static_cast<int>(plan_.layers.size());
        plan_.layers.push_back(std::move(layer));
        layer_instances_.push_back(std::move(layer_out));
        const PlanPass& built = plan_.layers.back();
        const Rect area{x0, y0, static_cast<float>(built.width), static_cast<float>(built.height)};
        const std::array<float, 4> texels{0, 0, area.width, area.height};

        if (command.shadow) {
            const Rect shadow_area{area.x + command.shadow->x * scale_, area.y + command.shadow->y * scale_,
                                   area.width, area.height};
            QuadInstance shadow;
            shadow.rect = {shadow_area.x, shadow_area.y, shadow_area.width, shadow_area.height};
            shadow.shape = texels;
            shadow.color = to_linear_premultiplied(command.shadow->color.opacity(command.opacity));
            shadow.params = {0, static_cast<float>(QuadKind::layer_shadow), 0, 0};
            emit(pass, out, shadow, shadow_area, clip, layer_index, true);
        }
        QuadInstance composite;
        composite.rect = {area.x, area.y, area.width, area.height};
        composite.shape = texels;
        composite.color = {0, 0, 0, std::clamp(command.opacity, 0.f, 1.f)};
        composite.params = {0, static_cast<float>(QuadKind::layer), 0, 0};
        emit(pass, out, composite, area, clip, layer_index, false);
    }

    // Builds a pass from commands[index...] until the PopLayerCommand closing it (or the end).
    // `origin` is the pass's top-left corner in window pixels.
    void build(std::size_t& index, Point origin, PlanPass& pass, std::vector<QuadInstance>& out) {
        std::vector<Clip> clips{Clip{}};
        for (; index < list_.commands.size(); ++index) {
            const DrawCommand& command = list_.commands[index];
            if (std::holds_alternative<PopLayerCommand>(command)) {
                return;
            }
            const Clip& clip = clips.back();
            std::visit(Overloaded{
                           [&](const RectCommand& c) { add_rect(c, origin, pass, out, clip); },
                           [&](const GlyphCommand& c) { add_glyph(c, origin, pass, out, clip); },
                           [&](const BoxShadowCommand& c) { add_box_shadow(c, origin, pass, out, clip); },
                           [&](const PathCommand& c) { add_path(c, origin, pass, out, clip); },
                           [&](const ImageCommand& c) { add_image(c, origin, pass, out, clip); },
                           [&](const PushClipCommand& c) { clips.push_back(push_clip(c, origin, clip)); },
                           [&](const PopClipCommand&) {
                               if (clips.size() > 1) {
                                   clips.pop_back();
                               }
                           },
                           [&](const PushLayerCommand& c) { add_layer(c, index, origin, pass, out, clip); },
                           [&](const PopLayerCommand&) {},
                       },
                       command);
        }
    }

    const DisplayList& list_;
    FramePlan& plan_;
    float scale_;
    std::vector<std::vector<QuadInstance>> layer_instances_;
};

} // namespace

FramePlan plan_frame(const DisplayList& list) {
    FramePlan plan;
    Planner(list, plan).run();
    return plan;
}

} // namespace elg::detail
