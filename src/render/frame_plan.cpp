// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include "render/frame_plan.h"

#include "core/numeric.h"
#include "render/color_space.h"
#include "text/atlas.h"

#include <algorithm>
#include <limits>
#include <cmath>
#include <numbers>

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

// Pixel positions and sizes from geometry, which may be NaN or infinite.
std::uint32_t to_pixel_count(float v) {
    return clamped_cast<std::uint32_t>(v, 0, std::numeric_limits<std::uint32_t>::max());
}
// Blurs run at a lower resolution once sigma passes this many texels: the result is the same
// (a blur that wide has no fine detail left) and the cost stays bounded, however wide the blur
// (ELGSPEC-0024.R3).
constexpr float kMaxBlurSigma = 12;
// How far a blur reaches, in sigmas: beyond 4 lies less than 1/30000 of a Gaussian, too little to
// show, also in the dark sRGB values where a small difference counts most (ELGSPEC-0024.R2).
constexpr float kBlurSigmas = 4;

// The clip in effect: `rect` with `radii` is the innermost rounded clip (at its own size; a plain
// rect when there's none), and `scissor` is the intersection of every enclosing clip's rect.
struct Clip {
    Rect rect{-kUnclipped, -kUnclipped, 2 * kUnclipped, 2 * kUnclipped};
    CornerRadii radii;
    Rect scissor{-kUnclipped, -kUnclipped, 2 * kUnclipped, 2 * kUnclipped};
    bool active = false;
};

bool rounded(const CornerRadii& r) {
    return r.top_left > 0 || r.top_right > 0 || r.bottom_right > 0 || r.bottom_left > 0;
}

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

// A power of 2: blurs halve the resolution that many times, 2 x 2 texels at a time.
std::uint32_t downsample_for(float sigma) {
    std::uint32_t factor = 1;
    while (sigma / static_cast<float>(factor) > kMaxBlurSigma && factor < (1u << 30)) {
        factor *= 2;
    }
    return factor;
}

constexpr float kDegrees = std::numbers::pi_v<float> / 180;

std::uint64_t fnv1a(const void* data, std::size_t size, std::uint64_t hash = 14695981039346656037ull) {
    const auto* bytes = static_cast<const unsigned char*>(data);
    for (std::size_t i = 0; i < size; ++i) {
        hash = (hash ^ bytes[i]) * 1099511628211ull;
    }
    return hash;
}

void set_clip(QuadInstance& instance, const Clip& clip) {
    instance.clip_rect = {clip.rect.x, clip.rect.y, clip.rect.width, clip.rect.height};
    instance.clip_radii = {clip.radii.top_left, clip.radii.top_right, clip.radii.bottom_right, clip.radii.bottom_left};
    instance.clip_scissor = {clip.scissor.x, clip.scissor.y, clip.scissor.width, clip.scissor.height};
}

// A composite of a layer image: `rect` in pass pixels shows `texels` of it.
QuadInstance layer_quad(const Rect& rect, const Rect& texels, float opacity, QuadKind kind = QuadKind::layer) {
    QuadInstance instance;
    instance.rect = {rect.x, rect.y, rect.width, rect.height};
    instance.shape = {texels.x, texels.y, texels.max_x(), texels.max_y()};
    instance.color = {0, 0, 0, std::clamp(opacity, 0.f, 1.f)};
    instance.params = {0, static_cast<float>(kind), 0, 0};
    instance.gradient = {1, 0, 0, 1}; // no transform
    return instance;
}

class Planner {
public:
    Planner(const DisplayList& list, FramePlan& plan, std::uint32_t width, std::uint32_t height, PathHashes& paths,
            std::uint32_t max_texture_side, std::optional<Rect> area)
        : list_(list), plan_(plan), paths_(paths), scale_(list.scale),
          max_side_(std::max(1u, std::min(kMaxLayerSize, max_texture_side))), area_(area) {
        plan_.main.width = width;
        plan_.main.height = height;
    }

    void run() {
        std::vector<QuadInstance> main_instances;
        std::size_t index = 0;
        const Rect window{0, 0, static_cast<float>(plan_.main.width), static_cast<float>(plan_.main.height)};
        // Only the area, when there is one: as a clip around everything, which the shader also
        // keeps every pixel within.
        Clip root;
        if (area_) {
            root.scissor = intersect(window, *area_);
            root.active = true;
        }
        if (area_) {
            // What the window held there goes: the area is drawn again from its background. (A
            // mask that keeps nothing, then the background over it, so a clear background works.)
            QuadInstance clear;
            clear.rect = {root.scissor.x, root.scissor.y, root.scissor.width, root.scissor.height};
            clear.params = {0, static_cast<float>(QuadKind::rect), static_cast<float>(PaintKind::solid), 0};
            emit(plan_.main, main_instances, clear, root.scissor, root, {.mask = true});
            clear.color = to_linear_premultiplied(list_.clear_color);
            if (clear.color[3] > 0) {
                emit(plan_.main, main_instances, clear, root.scissor, root, {});
            }
        }
        build(index, {0, 0}, plan_.main, main_instances, area_ ? root.scissor : window, root);

        // One instance buffer: layers first, in render order, then the main pass.
        for (std::size_t i = 0; i < plan_.layers.size(); ++i) {
            append(plan_.layers[i], layer_instances_[i]);
        }
        append(plan_.main, main_instances);
        if (plan_.main.reads_backdrop) {
            // The window draws offscreen; one composite copies it over.
            const auto w = static_cast<float>(plan_.main.width);
            const auto h = static_cast<float>(plan_.main.height);
            QuadInstance blit = layer_quad({0, 0, w, h}, {0, 0, w, h}, 1);
            set_clip(blit, Clip{});
            plan_.blit_instance = static_cast<std::uint32_t>(plan_.instances.size());
            plan_.instances.push_back(blit);
        }
        if (plan_.stops.empty()) {
            plan_.stops.push_back({0, 0, 0, 0}); // the stops buffer must never be empty
            plan_.stops.push_back({0, 0, 0, 0});
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
    [[nodiscard]] Rect to_pixels(const Rect& r, Point origin) const {
        return {r.x * scale_ - origin.x, r.y * scale_ - origin.y, r.width * scale_, r.height * scale_};
    }

    struct Emit {
        int layer = -1;
        LayerImage layer_image = LayerImage::content;
        int path = -1;
        bool mask = false;
        int image = -1;
        int video = -1;
        std::optional<PlanBackdrop> backdrop; // registered if the instance is drawn
    };

    void emit(PlanPass& pass, std::vector<QuadInstance>& out, QuadInstance instance, const Rect& extent,
              const Clip& clip, Emit how) {
        if (clip.active && (!overlaps(extent, clip.scissor) || !overlaps(extent, clip.rect))) {
            return; // fully clipped
        }
        set_clip(instance, clip);
        const auto index = static_cast<std::uint32_t>(out.size());
        out.push_back(instance);

        int backdrop = -1;
        if (how.backdrop) {
            backdrop = static_cast<int>(plan_.backdrops.size());
            plan_.backdrops.push_back(*how.backdrop);
            pass.reads_backdrop = true;
        }
        PlanSegment* last = pass.segments.empty() ? nullptr : &pass.segments.back();
        // A mask ("destination-in") is never plain: merged into the segment before it, it would be
        // drawn as a fill.
        const bool plain =
            !how.mask && how.layer < 0 && how.path < 0 && how.image < 0 && how.video < 0 && backdrop < 0;
        const auto last_plain = [&] {
            return last->layer < 0 && last->path < 0 && last->image < 0 && last->video < 0;
        };
        if (last && last->first + last->count == index &&
            ((plain && last_plain() && !last->mask) || (how.image >= 0 && last->image == how.image) ||
             (how.video >= 0 && last->video == how.video))) {
            ++last->count;
        } else {
            pass.segments.push_back(
                {index, 1, how.layer, how.layer_image, how.path, how.mask, how.image, how.video, backdrop});
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

    // Records a color matrix (six entries, keeping stops in pairs); returns its index + 1, the
    // form instances carry (0: none).
    float add_matrix(const ColorMatrix& m) {
        const auto index = static_cast<float>(plan_.stops.size());
        for (std::size_t row = 0; row < 4; ++row) {
            plan_.stops.push_back({m[row * 5], m[row * 5 + 1], m[row * 5 + 2], m[row * 5 + 3]});
        }
        plan_.stops.push_back({m[4], m[9], m[14], m[19]});
        plan_.stops.push_back({0, 0, 0, 0});
        return index + 1;
    }

    // The fill: a color, or a gradient whose unit points span `r` (pixels). Backdrops are drawn by
    // add_backdrop().
    void set_paint(QuadInstance& instance, const ShapeStyle& fill, const Rect& r) {
        const auto gradient = [&](const std::vector<GradientStop>& stops, PaintKind kind) {
            if (stops.empty()) {
                return false;
            }
            const auto [first, count] = add_stops(stops);
            instance.params[2] = static_cast<float>(kind);
            instance.params[3] = static_cast<float>(first);
            instance.extra[0] = static_cast<float>(count);
            return true;
        };
        std::visit(Overloaded{
                       [&](const Color& color) { instance.color = to_linear_premultiplied(color); },
                       [&](const LinearGradient& g) {
                           if (gradient(g.stops, PaintKind::linear)) {
                               instance.gradient = {r.x + g.start.x * r.width, r.y + g.start.y * r.height,
                                                    r.x + g.end.x * r.width, r.y + g.end.y * r.height};
                           }
                       },
                       [&](const RadialGradient& g) {
                           if (gradient(g.stops, PaintKind::radial)) {
                               instance.gradient = {r.x + g.center.x * r.width, r.y + g.center.y * r.height,
                                                    g.start_radius * scale_, g.end_radius * scale_};
                           }
                       },
                       [&](const EllipticalGradient& g) {
                           if (gradient(g.stops, PaintKind::elliptical)) {
                               instance.gradient = {r.x + g.center.x * r.width, r.y + g.center.y * r.height,
                                                    r.width, r.height};
                               instance.extra[2] = g.start_radius_fraction;
                               instance.extra[3] = g.end_radius_fraction;
                           }
                       },
                       [&](const AngularGradient& g) {
                           if (gradient(g.stops, PaintKind::angular)) {
                               instance.gradient = {r.x + g.center.x * r.width, r.y + g.center.y * r.height,
                                                    g.start_angle * kDegrees, g.end_angle * kDegrees};
                           }
                       },
                       [&](const BackdropFill&) { instance.color = {}; },
                   },
                   fill.value());
    }

    // A backdrop fill: the pass so far, snapshotted under the shape, blurred and color-adjusted.
    // `instance` has the shape; a border is drawn over it in its own color.
    void add_backdrop(const BackdropFill& backdrop_fill, QuadInstance instance, const Rect& extent, PlanPass& pass,
                      std::vector<QuadInstance>& out, const Clip& clip, int path) {
        const float sigma = std::max(0.f, backdrop_fill.blur * scale_ / 2);
        const float reach = blur_reach(sigma);
        const Rect bounds{0, 0, static_cast<float>(pass.width), static_cast<float>(pass.height)};
        const Rect wanted = intersect(extent.inset(-reach), bounds);
        const float x0 = std::floor(wanted.x);
        const float y0 = std::floor(wanted.y);
        const float x1 = std::ceil(wanted.max_x());
        const float y1 = std::ceil(wanted.max_y());
        if (x1 > x0 && y1 > y0) {
            PlanBackdrop backdrop;
            backdrop.x = to_pixel_count(x0);
            backdrop.y = to_pixel_count(y0);
            backdrop.width = to_pixel_count(x1 - x0);
            backdrop.height = to_pixel_count(y1 - y0);
            backdrop.sigma = sigma;
            backdrop.downsample = downsample_for(sigma);

            QuadInstance blurred = instance;
            blurred.params[0] = 0; // the border is drawn over it
            blurred.params[2] = static_cast<float>(PaintKind::backdrop);
            blurred.params[3] = backdrop_fill.saturation != 1 || backdrop_fill.brightness != 0
                                    ? add_matrix(backdrop_matrix(backdrop_fill))
                                    : 0.f;
            blurred.gradient = {x0, y0, static_cast<float>(backdrop.downsample),
                                std::clamp(backdrop_fill.noise, 0.f, 1.f)};
            emit(pass, out, blurred, extent, clip, {.path = path, .backdrop = backdrop});
        }
        if (instance.params[0] > 0) {
            instance.color = {};
            emit(pass, out, instance, extent, clip, {.path = path});
        }
    }

    // Saturation, then brightness.
    static ColorMatrix backdrop_matrix(const BackdropFill& fill) {
        const ColorMatrix s = color_matrix(ColorFilter::Kind::saturation, fill.saturation, {});
        ColorMatrix m = s;
        for (std::size_t row = 0; row < 3; ++row) {
            m[row * 5 + 4] += fill.brightness;
        }
        return m;
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
        if (const auto* backdrop = std::get_if<BackdropFill>(&command.fill.value())) {
            add_backdrop(*backdrop, instance, r.inset(-1), pass, out, clip, -1);
            return;
        }
        set_paint(instance, command.fill, r);
        emit(pass, out, instance, r.inset(-1), clip, {});
    }

    // A path: its fan triangles go to a coverage mask; the pass fills through the mask. With
    // `mask_pass` (a pass's size in pixels), the path instead masks what the pass has drawn so far:
    // a quad over the whole pass keeps only what the coverage covers.
    // `visible`: the pass's part that can show, in its pixels; the mask covers only what shows of
    // the path there, through the clip (ELGSPEC-0024.R5, R6).
    void add_path(const PathCommand& command, Point origin, PlanPass& pass, std::vector<QuadInstance>& out,
                  const Clip& clip, const Rect& visible, std::optional<Size> mask_pass = std::nullopt) {
        // A mask that covers no pixel keeps nothing of the pass (ELGSPEC-0024.R4): a path with no
        // area, a shape scaled to zero, or one at a position that isn't finite.
        const auto keep_nothing = [&](Size size) {
            QuadInstance clear;
            clear.rect = {0, 0, size.width, size.height};
            clear.params = {0, static_cast<float>(QuadKind::rect), static_cast<float>(PaintKind::solid), 0};
            emit(pass, out, clear, clear_extent(size), Clip{}, {.mask = true});
        };
        if (!command.polygons || command.polygons->empty()) {
            if (mask_pass) {
                keep_nothing(*mask_pass);
            }
            return;
        }
        const Rect bounds = to_pixels(command.window_bounds(), origin);
        if (!(bounds.width > 0) || !(bounds.height > 0)) { // no area, or not finite
            if (mask_pass) {
                keep_nothing(*mask_pass);
            }
            return;
        }
        // The whole path, with a pixel around it for anti-aliasing, then only what can show of it.
        const float full_x0 = std::floor(bounds.x) - 1;
        const float full_y0 = std::floor(bounds.y) - 1;
        const Rect shown = mask_pass ? clear_extent(*mask_pass) : visible_through(visible, clip);
        const float x0 = std::max(full_x0, std::floor(shown.x));
        const float y0 = std::max(full_y0, std::floor(shown.y));
        const float x1 = std::min(std::ceil(bounds.max_x()) + 1, std::ceil(shown.max_x()));
        const float y1 = std::min(std::ceil(bounds.max_y()) + 1, std::ceil(shown.max_y()));
        const Rect area{x0, y0, std::min(x1 - x0, static_cast<float>(max_side_)),
                        std::min(y1 - y0, static_cast<float>(max_side_))};
        // Written so that NaN (a path with non-finite points) is skipped too.
        if (!mask_pass && (!(area.width >= 1) || !(area.height >= 1))) {
            return; // nothing of it shows
        }

        PlanPath path;
        path.width = clamped_cast<std::uint32_t>(area.width, 1, max_side_);
        path.height = clamped_cast<std::uint32_t>(area.height, 1, max_side_);
        path.even_odd = command.rule == FillRule::even_odd;
        const float k = command.scale * scale_;
        // Sub-pixel positions snap to a quarter pixel (at most 1/8 px off, invisible), so a moving
        // path (scrolling, animations) cycles through a few cached masks instead of a new one
        // every frame.
        const auto snap = [](float v) { return std::floor(v) + std::round((v - std::floor(v)) * 4) / 4; };
        const float dx = snap(command.offset.x * scale_ - origin.x - x0);
        const float dy = snap(command.offset.y * scale_ - origin.y - y0);
        path.polygons = command.polygons;
        path.scale = k;
        path.offset = {dx, dy};

        // The content key: the polygons' hash, then everything that changes how they rasterize.
        // The integer part of the position doesn't (the mask moves with the quad), the fraction
        // does, and so does which part of the path the mask holds, when it holds only part.
        const PathHashes::Hash hash = paths_.get(command.polygons);
        const float subpixel_x = dx - std::floor(dx);
        const float subpixel_y = dy - std::floor(dy);
        const std::array<float, 5> placement{k, subpixel_x, subpixel_y, x0 - full_x0, y0 - full_y0};
        const std::array<std::uint32_t, 3> shape{path.width, path.height,
                                                  command.rule == FillRule::even_odd ? 1u : 0u};
        path.key = fnv1a(shape.data(), sizeof(shape), fnv1a(placement.data(), sizeof(placement), hash.content));
        // Written so that NaN is caught too.
        if (mask_pass && (hash.vertex_count == 0 || !(area.width >= 1) || !(area.height >= 1))) {
            keep_nothing(*mask_pass);
            return;
        }
        if (hash.vertex_count == 0) {
            return;
        }
        const int index = static_cast<int>(plan_.paths.size());
        plan_.paths.push_back(std::move(path));

        if (mask_pass) {
            // Over the whole pass; outside the mask's own area the sampler clamps to its empty
            // border.
            QuadInstance instance;
            instance.rect = {0, 0, mask_pass->width, mask_pass->height};
            instance.shape = {-area.x, -area.y, mask_pass->width - area.x, mask_pass->height - area.y};
            instance.color = {1, 1, 1, 1};
            instance.params = {0, static_cast<float>(QuadKind::path), static_cast<float>(PaintKind::solid), 0};
            emit(pass, out, instance, clear_extent(*mask_pass), Clip{}, {.path = index, .mask = true});
            return;
        }

        QuadInstance instance;
        instance.rect = {area.x, area.y, area.width, area.height};
        instance.shape = {0, 0, area.width, area.height};
        instance.params = {0, static_cast<float>(QuadKind::path), static_cast<float>(PaintKind::solid), 0};
        if (const auto* backdrop = std::get_if<BackdropFill>(&command.fill.value())) {
            add_backdrop(*backdrop, instance, area, pass, out, clip, index);
            return;
        }
        set_paint(instance, command.fill, to_pixels(command.paint_frame, origin));
        emit(pass, out, instance, area, clip, {.path = index});
    }

    static Rect clear_extent(Size size) { return {0, 0, size.width, size.height}; }

    // What of a pass's `visible` part shows through `clip`, in the pass's pixels.
    static Rect visible_through(const Rect& visible, const Clip& clip) {
        const Rect scissored = intersect(visible, clip.scissor);
        return clip.active ? intersect(scissored, clip.rect) : scissored;
    }

    void add_video(const VideoCommand& command, Point origin, PlanPass& pass, std::vector<QuadInstance>& out,
                   const Clip& clip) {
        if (!command.source || command.opacity <= 0) {
            return;
        }
        Rect r = to_pixels(command.rect, origin);
        if (r.width <= 0 || r.height <= 0) {
            return;
        }
        int index = -1;
        for (std::size_t i = 0; i < plan_.videos.size(); ++i) {
            if (plan_.videos[i] == command.source) {
                index = static_cast<int>(i);
                break;
            }
        }
        if (index < 0) {
            index = static_cast<int>(plan_.videos.size());
            plan_.videos.push_back(command.source);
            plan_.video_frames.push_back(command.source->current());
        }
        const auto& frame = plan_.video_frames[static_cast<std::size_t>(index)];
        if (command.content_mode) {
            if (!frame || frame->width == 0 || frame->height == 0) {
                return;
            }
            const float sx = r.width / static_cast<float>(frame->width);
            const float sy = r.height / static_cast<float>(frame->height);
            const float scale = *command.content_mode == ContentMode::fit ? std::min(sx, sy) : std::max(sx, sy);
            const float width = static_cast<float>(frame->width) * scale;
            const float height = static_cast<float>(frame->height) * scale;
            r = {r.x + (r.width - width) / 2, r.y + (r.height - height) / 2, width, height};
        }
        QuadInstance instance;
        instance.rect = {r.x, r.y, r.width, r.height};
        instance.shape = {command.uv.x, command.uv.y, command.uv.max_x(), command.uv.max_y()};
        instance.color = {0, 0, 0, std::clamp(command.opacity, 0.f, 1.f)};
        // Video frames and GPU images have straight alpha: the shader premultiplies it to filter them.
        instance.params = {1, static_cast<float>(QuadKind::image), 0, 0};
        emit(pass, out, instance, r, clip, {.video = index});
    }

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
        emit(pass, out, instance, r, clip, {.image = index});
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
        emit(pass, out, instance, q, clip, {});
    }

    void add_box_shadow(const BoxShadowCommand& command, Point origin, PlanPass& pass,
                        std::vector<QuadInstance>& out, const Clip& clip) {
        const Rect r = to_pixels(command.rect, origin);
        const float sigma = std::max(0.f, command.blur_radius * scale_ / 2);
        QuadInstance instance;
        instance.rect = {r.x, r.y, r.width, r.height};
        instance.shape = scaled_radii(command.radii, scale_, r.width, r.height);
        instance.color = to_linear_premultiplied(command.color);
        instance.params = {0, static_cast<float>(QuadKind::box_shadow), 0, 0};
        instance.extra = {0, sigma, 0, 0};
        emit(pass, out, instance, r.inset(-kBlurSigmas * sigma - 1), clip, {});
    }

    void add_inner_shadow(const InnerShadowCommand& command, Point origin, PlanPass& pass,
                          std::vector<QuadInstance>& out, const Clip& clip) {
        const Rect r = to_pixels(command.rect, origin);
        const float sigma = std::max(0.f, command.blur_radius * scale_ / 2);
        QuadInstance instance;
        instance.rect = {r.x, r.y, r.width, r.height};
        instance.shape = scaled_radii(command.radii, scale_, r.width, r.height);
        instance.color = to_linear_premultiplied(command.color);
        instance.border_color = {command.offset.x * scale_, command.offset.y * scale_, 0, 0};
        instance.params = {command.spread * scale_, static_cast<float>(QuadKind::inner_shadow), 0, 0};
        instance.extra = {0, sigma, 0, 0};
        emit(pass, out, instance, r.inset(-1), clip, {});
    }

    [[nodiscard]] Clip push_clip(const PushClipCommand& command, Point origin, const Clip& current) const {
        const Rect r = to_pixels(command.rect, origin);
        const std::array<float, 4> radii = scaled_radii(command.radii, scale_, r.width, r.height);
        const CornerRadii corners{radii[0], radii[1], radii[2], radii[3]};
        Clip clip = current;
        clip.active = true;
        if (!rounded(corners)) {
            // A plain rect only narrows the scissor; an enclosing rounded clip keeps its shape.
            clip.scissor = intersect(current.scissor, r);
            return clip;
        }
        // A rounded clip becomes the shape, at its own size. The enclosing shape's rect joins the
        // scissor. Exact unless both are rounded and overlap near the outer one's corners; then the
        // outer corners are approximated by its rect.
        clip.scissor = current.active ? intersect(current.scissor, current.rect) : current.scissor;
        if (current.active && rounded(current.radii) && contains(r, current.rect)) {
            return current; // the new clip contains the old one: nothing changes
        }
        clip.rect = r;
        clip.radii = corners;
        return clip;
    }

    // Moves `index` from a layer's PushLayerCommand to its PopLayerCommand, past its content and
    // its mask: for a layer that draws nothing.
    void skip_layer(std::size_t& index) const {
        std::size_t depth = 1;
        for (++index; index < list_.commands.size(); ++index) {
            const DrawCommand& command = list_.commands[index];
            if (std::holds_alternative<PushLayerCommand>(command)) {
                ++depth;
            } else if (std::holds_alternative<PopLayerCommand>(command) && --depth == 0) {
                return;
            }
        }
    }

    // The part of a layer's image that what shows of it needs, in the pass's pixels before the
    // layer's transform: where its composite shows (`shown`), and around that what its blur and
    // its shadows reach. None when nothing shows: `shown` has no area, or the transform collapses
    // the layer.
    [[nodiscard]] std::optional<Rect> needed_region(const PushLayerCommand& command, const Rect& shown,
                                                    const std::optional<LayerTransform>& transform, float shadow_sigma,
                                                    std::optional<float> blur_sigma,
                                                    std::optional<float> inner_sigma) const {
        if (!(shown.width > 0) || !(shown.height > 0)) {
            return std::nullopt;
        }
        Rect v = shown;
        if (transform) {
            // Where the composite's points come from: the transform's inverse, about its pivot.
            const float det = transform->a * transform->d - transform->b * transform->c;
            if (!(std::abs(det) > 1e-6f)) {
                return std::nullopt;
            }
            const Point pivot = transform->pivot;
            const auto from = [&](float x, float y) {
                const float qx = x - pivot.x;
                const float qy = y - pivot.y;
                return Point{pivot.x + (transform->d * qx - transform->c * qy) / det,
                             pivot.y + (-transform->b * qx + transform->a * qy) / det};
            };
            const std::array<Point, 4> corners{from(shown.x, shown.y), from(shown.max_x(), shown.y),
                                               from(shown.x, shown.max_y()), from(shown.max_x(), shown.max_y())};
            float min_x = corners[0].x;
            float min_y = corners[0].y;
            float max_x = corners[0].x;
            float max_y = corners[0].y;
            for (const Point& p : corners) {
                min_x = std::min(min_x, p.x);
                min_y = std::min(min_y, p.y);
                max_x = std::max(max_x, p.x);
                max_y = std::max(max_y, p.y);
            }
            v = {min_x, min_y, max_x - min_x, max_y - min_y};
        }
        const auto unite = [](const Rect& a, const Rect& b) {
            const float x0 = std::min(a.x, b.x);
            const float y0 = std::min(a.y, b.y);
            return Rect{x0, y0, std::max(a.max_x(), b.max_x()) - x0, std::max(a.max_y(), b.max_y()) - y0};
        };
        const auto reach = blur_reach;
        Rect needed = v;
        if (blur_sigma) {
            needed = unite(needed, v.inset(-reach(*blur_sigma)));
        }
        if (command.shadow) {
            const Rect source{v.x - command.shadow->x * scale_, v.y - command.shadow->y * scale_, v.width, v.height};
            needed = unite(needed, source.inset(-reach(shadow_sigma)));
        }
        if (command.inner_shadow && inner_sigma) {
            const Rect source{v.x - command.inner_shadow->x * scale_, v.y - command.inner_shadow->y * scale_, v.width,
                              v.height};
            needed = unite(needed, source.inset(-reach(*inner_sigma)));
        }
        return needed;
    }

    // `visible`: the pass's part that can show, in its pixels. The layer's image covers only what
    // shows of it there, through the clip, and what its blur and shadows reach into from around
    // that (ELGSPEC-0024.R5, R6); a layer of which nothing shows gets none.
    void add_layer(const PushLayerCommand& command, std::size_t& index, Point origin, PlanPass& pass,
                   std::vector<QuadInstance>& out, const Clip& clip, const Rect& visible) {
        // Sigmas in pixels: half the radius, at the window's scale; 0 leaves the content as it is.
        const float sigma = command.shadow ? std::max(0.f, command.shadow->radius * scale_ / 2) : 0.f;
        // Room around the content for anti-aliasing and the blurs.
        float margin = command.shadow ? std::ceil(kBlurSigmas * sigma) + 2 : 1;
        std::optional<float> blur_sigma;
        if (command.blur > 0) {
            blur_sigma = command.blur * scale_ / 2;
            if (!command.blur_opaque) {
                margin = std::max(margin, std::ceil(kBlurSigmas * *blur_sigma) + 2);
            }
        }
        // The transform, in this pass's pixels.
        std::optional<LayerTransform> transform;
        if (command.transform) {
            transform = *command.transform;
            transform->pivot = {command.transform->pivot.x * scale_ - origin.x,
                                command.transform->pivot.y * scale_ - origin.y};
        }
        const std::uint32_t blur_downsample = blur_sigma ? downsample_for(*blur_sigma) : 1;
        const std::optional<float> inner_sigma =
            command.inner_shadow ? std::optional(std::max(0.f, command.inner_shadow->radius * scale_ / 2))
                                 : std::nullopt;
        const std::optional<Rect> needed =
            needed_region(command, visible_through(visible, clip), transform, sigma, blur_sigma, inner_sigma);
        const Rect content = to_pixels(command.bounds, origin);
        float x0 = std::floor(content.x - margin);
        float y0 = std::floor(content.y - margin);
        float x1 = std::ceil(content.max_x() + margin);
        float y1 = std::ceil(content.max_y() + margin);
        if (needed) {
            x0 = std::max(x0, std::floor(needed->x));
            y0 = std::max(y0, std::floor(needed->y));
            x1 = std::min(x1, std::ceil(needed->max_x()));
            y1 = std::min(y1, std::ceil(needed->max_y()));
        }
        // Written so that NaN is caught too.
        if (!needed || !(x1 > x0) || !(y1 > y0)) {
            skip_layer(index);
            return; // nothing of it shows: no image
        }
        const auto clamp_size = [&](float v) {
            return clamped_cast<std::uint32_t>(v, 1, max_side_);
        };
        PlanPass layer;
        layer.width = clamp_size(x1 - x0);
        layer.height = clamp_size(y1 - y0);
        if (command.shadow) {
            layer.shadow_sigma = sigma;
            layer.shadow_downsample = downsample_for(sigma);
        }
        if (blur_sigma) {
            layer.blur_sigma = blur_sigma;
            layer.blur_downsample = blur_downsample;
            layer.blur_clamp = command.blur_opaque;
        }
        layer.inner_shadow_sigma = inner_sigma;
        if (inner_sigma) {
            layer.inner_shadow_downsample = downsample_for(*inner_sigma);
        }

        std::vector<QuadInstance> layer_out;
        ++index;
        const Point layer_origin{origin.x + x0, origin.y + y0};
        const Size layer_size{static_cast<float>(layer.width), static_cast<float>(layer.height)};
        build(index, layer_origin, layer, layer_out, clear_extent(layer_size));
        if (index < list_.commands.size() && std::holds_alternative<LayerMaskCommand>(list_.commands[index])) {
            // The mask: its own layer, the same size, whose alpha then keeps part of this one.
            PlanPass mask_layer;
            mask_layer.width = layer.width;
            mask_layer.height = layer.height;
            std::vector<QuadInstance> mask_out;
            ++index;
            build(index, layer_origin, mask_layer, mask_out, clear_extent(layer_size));
            const int mask_index = static_cast<int>(plan_.layers.size());
            plan_.layers.push_back(std::move(mask_layer));
            layer_instances_.push_back(std::move(mask_out));
            const Rect whole{0, 0, layer_size.width, layer_size.height};
            emit(layer, layer_out, layer_quad(whole, whole, 1), whole, Clip{}, {.layer = mask_index, .mask = true});
        }
        if (command.mask) {
            add_path(*command.mask, layer_origin, layer, layer_out, Clip{}, clear_extent(layer_size), layer_size);
        }

        const int layer_index = static_cast<int>(plan_.layers.size());
        plan_.layers.push_back(std::move(layer));
        layer_instances_.push_back(std::move(layer_out));
        const PlanPass& built = plan_.layers.back();
        const Rect area{x0, y0, static_cast<float>(built.width), static_cast<float>(built.height)};
        const Rect texels{0, 0, area.width, area.height};

        const auto place = [&](QuadInstance& instance) {
            if (transform) {
                instance.gradient = {transform->a, transform->b, transform->c, transform->d};
                instance.extra[2] = transform->pivot.x;
                instance.extra[3] = transform->pivot.y;
            }
        };
        const auto extent_of = [&](const Rect& r) { return transform ? transformed_bounds(*transform, r) : r; };

        if (command.shadow) {
            const Rect shadow_area{area.x + command.shadow->x * scale_, area.y + command.shadow->y * scale_,
                                   area.width, area.height};
            const auto shadow_downsample = static_cast<float>(built.shadow_downsample);
            QuadInstance shadow = layer_quad(
                shadow_area, {0, 0, area.width / shadow_downsample, area.height / shadow_downsample}, 1,
                QuadKind::layer_shadow);
            shadow.color = to_linear_premultiplied(command.shadow->color.opacity(command.opacity));
            place(shadow);
            emit(pass, out, shadow, extent_of(shadow_area), clip, {.layer = layer_index, .layer_image = LayerImage::shadow});
        }

        const auto downsample = static_cast<float>(built.blur_downsample);
        QuadInstance composite =
            layer_quad(area, {0, 0, area.width / downsample, area.height / downsample}, command.opacity);
        if (command.color_matrix) {
            composite.params[0] = add_matrix(*command.color_matrix);
        }
        place(composite);
        Emit how{.layer = layer_index};
        if (command.blend != BlendMode::normal) {
            // Blending reads what's behind: the pass so far, under the composite.
            const Rect bounds{0, 0, static_cast<float>(pass.width), static_cast<float>(pass.height)};
            const Rect under = intersect(extent_of(area), bounds);
            const float bx0 = std::floor(under.x);
            const float by0 = std::floor(under.y);
            const float bx1 = std::ceil(under.max_x());
            const float by1 = std::ceil(under.max_y());
            if (bx1 <= bx0 || by1 <= by0) {
                return; // nothing of it is in the pass
            }
            PlanBackdrop backdrop;
            backdrop.x = to_pixel_count(bx0);
            backdrop.y = to_pixel_count(by0);
            backdrop.width = to_pixel_count(bx1 - bx0);
            backdrop.height = to_pixel_count(by1 - by0);
            how.backdrop = backdrop;
            composite.params[2] = static_cast<float>(command.blend);
            composite.border_color = {bx0, by0, 1, 0};
        }
        emit(pass, out, composite, extent_of(area), clip, how);

        if (command.inner_shadow) {
            QuadInstance inner = layer_quad(area, texels, 1, QuadKind::layer_inner_shadow);
            inner.color = to_linear_premultiplied(command.inner_shadow->color.opacity(command.opacity));
            // The offset, and the scale of the blurred image, which may be downsampled.
            inner.border_color = {command.inner_shadow->x * scale_, command.inner_shadow->y * scale_,
                                  1 / static_cast<float>(built.inner_shadow_downsample), 0};
            place(inner);
            emit(pass, out, inner, extent_of(area), clip,
                 {.layer = layer_index, .layer_image = LayerImage::inner_shadow});
        }
    }

    // Builds a pass from commands[index...] until the PopLayerCommand closing it, its
    // LayerMaskCommand, or the end. `origin` is the pass's top-left corner in window pixels.
    // `visible`: the part of the pass that can show, in its pixels.
    void build(std::size_t& index, Point origin, PlanPass& pass, std::vector<QuadInstance>& out,
               const Rect& visible, const Clip& root = Clip{}) {
        std::vector<Clip> clips{root};
        for (; index < list_.commands.size(); ++index) {
            const DrawCommand& command = list_.commands[index];
            if (std::holds_alternative<PopLayerCommand>(command) || std::holds_alternative<LayerMaskCommand>(command)) {
                return;
            }
            const Clip& clip = clips.back();
            std::visit(Overloaded{
                           [&](const RectCommand& c) { add_rect(c, origin, pass, out, clip); },
                           [&](const GlyphCommand& c) { add_glyph(c, origin, pass, out, clip); },
                           [&](const BoxShadowCommand& c) { add_box_shadow(c, origin, pass, out, clip); },
                           [&](const InnerShadowCommand& c) { add_inner_shadow(c, origin, pass, out, clip); },
                           [&](const PathCommand& c) { add_path(c, origin, pass, out, clip, visible); },
                           [&](const ImageCommand& c) { add_image(c, origin, pass, out, clip); },
                           [&](const VideoCommand& c) { add_video(c, origin, pass, out, clip); },
                           [&](const PushClipCommand& c) { clips.push_back(push_clip(c, origin, clip)); },
                           [&](const PopClipCommand&) {
                               if (clips.size() > 1) {
                                   clips.pop_back();
                               }
                           },
                           [&](const PushLayerCommand& c) {
                               add_layer(c, index, origin, pass, out, clip, visible);
                           },
                           [&](const LayerMaskCommand&) {},
                           [&](const PopLayerCommand&) {},
                       },
                       command);
        }
    }

    const DisplayList& list_;
    FramePlan& plan_;
    PathHashes& paths_;
    float scale_;
    std::uint32_t max_side_; // of layers and path masks
    std::optional<Rect> area_; // of the window, which alone it draws
    std::vector<std::vector<QuadInstance>> layer_instances_;
};

} // namespace

std::vector<QuadInstance> window_overlay(std::uint32_t width, std::uint32_t height, std::span<const Rect> tinted) {
    const Rect window{0, 0, static_cast<float>(width), static_cast<float>(height)};
    std::vector<QuadInstance> instances{layer_quad(window, window, 1)};
    instances[0].params[3] = 1; // a copy: texel for texel
    // Magenta, a quarter opaque: shows over light content and dark alike.
    const std::array<float, 4> tint = to_linear_premultiplied(Color{1, 0, 1, 0.25f});
    for (const Rect& r : tinted) {
        QuadInstance instance;
        instance.rect = {r.x, r.y, r.width, r.height};
        instance.color = tint;
        instance.params = {0, static_cast<float>(QuadKind::rect), static_cast<float>(PaintKind::solid), 0};
        instances.push_back(instance);
    }
    for (QuadInstance& instance : instances) {
        set_clip(instance, Clip{});
    }
    return instances;
}

float blur_reach(float sigma) {
    return std::ceil(kBlurSigmas * sigma) + 2 + static_cast<float>(downsample_for(sigma));
}

void append_path_fan(const PlanPath& path, std::vector<std::array<float, 2>>& vertices) {
    const float k = path.scale;
    const float dx = path.offset[0];
    const float dy = path.offset[1];
    const auto vertex = [&](Point p) { return std::array<float, 2>{p.x * k + dx, p.y * k + dy}; };
    for (const std::vector<Point>& polygon : *path.polygons) {
        // A fan from the first point: overlapping triangles cancel or add up in the stencil
        // exactly as the polygon winds.
        for (std::size_t i = 1; i + 1 < polygon.size(); ++i) {
            vertices.push_back(vertex(polygon[0]));
            vertices.push_back(vertex(polygon[i]));
            vertices.push_back(vertex(polygon[i + 1]));
        }
    }
}

PathHashes::Hash PathHashes::get(const std::shared_ptr<const Polygons>& polygons) {
    Entry& entry = entries_[polygons.get()];
    // The same set, not another one made where an earlier set was.
    const bool same = !entry.polygons.owner_before(polygons) && !polygons.owner_before(entry.polygons);
    if (!same || entry.polygons.expired()) {
        Hash hash;
        std::uint64_t content = 14695981039346656037ull;
        std::size_t vertices = 0;
        for (const std::vector<Point>& polygon : *polygons) {
            content = fnv1a(polygon.data(), polygon.size() * sizeof(Point), content);
            const std::size_t count = polygon.size();
            content = fnv1a(&count, sizeof(count), content);
            vertices += count >= 3 ? 3 * (count - 2) : 0;
        }
        constexpr std::size_t kMaxVertices = std::numeric_limits<std::uint32_t>::max();
        hash.content = content;
        hash.vertex_count = static_cast<std::uint32_t>(std::min(vertices, kMaxVertices));
        entry = {polygons, hash, false};
        ++hashed_;
    }
    entry.used = true;
    return entry.hash;
}

void PathHashes::end_frame() {
    std::erase_if(entries_, [](const auto& entry) { return !entry.second.used; });
    for (auto& [polygons, entry] : entries_) {
        entry.used = false;
    }
}

FramePlan plan_frame(const DisplayList& list, std::uint32_t width, std::uint32_t height, PathHashes* paths,
                     std::uint32_t max_texture_side, std::optional<Rect> area) {
    FramePlan plan;
    PathHashes once;
    Planner(list, plan, width, height, paths ? *paths : once, max_texture_side, area).run();
    return plan;
}

} // namespace elg::detail
