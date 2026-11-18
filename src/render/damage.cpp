// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include "render/damage.h"

#include "media/frame_source.h"
#include "render/frame_plan.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <type_traits>
#include <utility>
#include <variant>

namespace elg::detail {

namespace {

template <class... Fs>
struct Overloaded : Fs... {
    using Fs::operator()...;
};

// 64-bit FNV-1a over what a command draws.
class Hasher {
public:
    explicit Hasher(std::uint64_t seed = 14695981039346656037ull) : value_(seed) {}

    Hasher& bytes(const void* data, std::size_t size) {
        const auto* p = static_cast<const unsigned char*>(data);
        for (std::size_t i = 0; i < size; ++i) {
            value_ = (value_ ^ p[i]) * 1099511628211ull;
        }
        return *this;
    }
    template <class T>
        requires std::is_arithmetic_v<T> || std::is_enum_v<T>
    Hasher& add(T v) {
        return bytes(&v, sizeof(v));
    }
    Hasher& add(const void* p) { return add(std::bit_cast<std::uintptr_t>(p)); }
    Hasher& add(Point p) { return add(p.x).add(p.y); }
    Hasher& add(UnitPoint p) { return add(p.x).add(p.y); }
    Hasher& add(const Rect& r) { return add(r.x).add(r.y).add(r.width).add(r.height); }
    Hasher& add(const Color& c) { return add(c.r).add(c.g).add(c.b).add(c.a); }
    Hasher& add(const CornerRadii& r) {
        return add(r.top_left).add(r.top_right).add(r.bottom_right).add(r.bottom_left);
    }
    Hasher& add(const std::vector<GradientStop>& stops) {
        add(stops.size());
        for (const GradientStop& stop : stops) {
            add(stop.color).add(stop.location);
        }
        return *this;
    }
    Hasher& add(const ShapeStyle& style) {
        add(style.value().index());
        std::visit(Overloaded{
                       [&](const Color& c) { add(c); },
                       [&](const LinearGradient& g) { add(g.stops).add(g.start).add(g.end); },
                       [&](const RadialGradient& g) {
                           add(g.stops).add(g.center).add(g.start_radius).add(g.end_radius);
                       },
                       [&](const EllipticalGradient& g) {
                           add(g.stops).add(g.center).add(g.start_radius_fraction).add(g.end_radius_fraction);
                       },
                       [&](const AngularGradient& g) {
                           add(g.stops).add(g.center).add(g.start_angle).add(g.end_angle);
                       },
                       [&](const BackdropFill& b) { add(b.blur).add(b.saturation).add(b.brightness).add(b.noise); },
                   },
                   style.value());
        return *this;
    }
    Hasher& add(const Shadow& s) { return add(s.color).add(s.radius).add(s.x).add(s.y).add(s.spread); }
    Hasher& add(const LayerTransform& t) { return add(t.a).add(t.b).add(t.c).add(t.d).add(t.pivot); }
    // The polygons by their points, from `paths`: hashed once while the same set is drawn.
    Hasher& add(const PathCommand& c, PathHashes& paths) {
        add(c.polygons ? paths.get(c.polygons).content : 0).add(c.bounds).add(c.scale).add(c.offset);
        return add(c.paint_frame).add(c.fill).add(c.rule);
    }

    [[nodiscard]] std::uint64_t value() const noexcept { return value_; }

private:
    std::uint64_t value_;
};

Rect intersect(const Rect& a, const Rect& b) {
    const float x0 = std::max(a.x, b.x);
    const float y0 = std::max(a.y, b.y);
    const float x1 = std::min(a.max_x(), b.max_x());
    const float y1 = std::min(a.max_y(), b.max_y());
    // Written so that NaN gives nothing.
    if (!(x1 > x0) || !(y1 > y0)) {
        return {};
    }
    return {x0, y0, x1 - x0, y1 - y0};
}

bool has_area(const Rect& r) {
    return r.width > 0 && r.height > 0;
}

Rect unite(const Rect& a, const Rect& b) {
    if (!has_area(a)) {
        return b;
    }
    if (!has_area(b)) {
        return a;
    }
    return union_of(a, b);
}

float area(const Rect& r) {
    return has_area(r) ? r.width * r.height : 0;
}

bool overlaps(const Rect& a, const Rect& b) {
    return has_area(a) && has_area(b) && a.x < b.max_x() && b.x < a.max_x() && a.y < b.max_y() && b.y < a.max_y();
}

// A layer around the commands that follow its PushLayerCommand, in pixels.
struct Level {
    std::vector<Rect> clips;             // the clip in effect inside it, innermost last
    // Beside each clip: the keys of the layers and clips around what's drawn there.
    std::vector<std::uint64_t> contexts;
    std::optional<LayerTransform> transform;
    float blur_reach = 0;                // how far its blur draws around its content
    std::optional<Point> shadow_offset;  // its shadow's offset
    float shadow_reach = 0;              // how far its shadow's blur draws around it
    std::optional<Rect> mask;            // what its mask path can keep
    Point offset;                        // where its (0, 0) is in the level around it (scrolls)
};

class Walker {
public:
    Walker(const DisplayList& list, std::uint32_t width, std::uint32_t height, PathHashes& paths)
        : list_(list), paths_(paths), scale_(list.scale),
          window_{0, 0, static_cast<float>(width), static_cast<float>(height)} {
        levels_.push_back({.clips = {window_}, .contexts = {14695981039346656037ull}});
    }

    std::vector<Footprint> run() {
        std::vector<Footprint> out;
        out.reserve(list_.commands.size());
        for (const DrawCommand& command : list_.commands) {
            out.push_back(visit(command));
        }
        return out;
    }

private:
    [[nodiscard]] Rect pixels(const Rect& r) const {
        return {r.x * scale_, r.y * scale_, r.width * scale_, r.height * scale_};
    }

    // `r`, in the innermost level's pixels, as it shows in the window: within each level's
    // clip and mask, widened by its effects, moved by its transform.
    [[nodiscard]] Rect up(Rect r) const {
        for (std::size_t i = levels_.size(); i-- > 1;) {
            const Level& level = levels_[i];
            r = intersect(r, level.clips.back());
            if (level.mask) {
                r = intersect(r, *level.mask);
            }
            if (!has_area(r)) {
                return {};
            }
            Rect widened = r.inset(-level.blur_reach);
            if (level.shadow_offset) {
                const Rect shadow{r.x + level.shadow_offset->x, r.y + level.shadow_offset->y, r.width, r.height};
                widened = unite(widened, shadow.inset(-level.shadow_reach));
            }
            r = level.transform ? transformed_bounds(*level.transform, widened) : widened;
            r.x += level.offset.x;
            r.y += level.offset.y;
        }
        r = intersect(r, levels_.front().clips.back());
        return round_out(intersect(r, window_));
    }

    static Rect round_out(const Rect& r) {
        if (!has_area(r)) {
            return {};
        }
        const float x0 = std::floor(r.x);
        const float y0 = std::floor(r.y);
        return {x0, y0, std::ceil(r.max_x()) - x0, std::ceil(r.max_y()) - y0};
    }

    [[nodiscard]] std::uint64_t context() const { return levels_.back().contexts.back(); }

    // A shape's footprint: anti-aliasing reaches a pixel past its geometry.
    [[nodiscard]] Footprint shape(std::uint64_t key, const Rect& points, const ShapeStyle* fill) const {
        Footprint print{key, up(pixels(points).inset(-1)), std::nullopt};
        if (fill) {
            if (const auto* backdrop = std::get_if<BackdropFill>(&fill->value())) {
                // It reads what's behind it, as far as its blur reaches.
                const float sigma = std::max(0.f, backdrop->blur * scale_ / 2);
                print.reads = up(pixels(points).inset(-1 - blur_reach(sigma)));
            }
        }
        return print;
    }

    Footprint visit(const DrawCommand& command) {
        const std::uint64_t index = command.index();
        Hasher key(context());
        key.add(index);
        return std::visit(
            Overloaded{
                [&](const RectCommand& c) {
                    key.add(c.rect).add(c.radii).add(c.fill).add(c.border).add(c.border_width);
                    return shape(key.value(), c.rect, &c.fill);
                },
                [&](const GlyphCommand& c) {
                    key.add(c.quad).add(c.uv).add(c.color).add(c.em_size);
                    return shape(key.value(), c.quad, nullptr);
                },
                [&](const BoxShadowCommand& c) {
                    key.add(c.rect).add(c.radii).add(c.color).add(c.blur_radius);
                    return shape(key.value(), *draw_bounds(command), nullptr);
                },
                [&](const InnerShadowCommand& c) {
                    key.add(c.rect).add(c.radii).add(c.color).add(c.blur_radius).add(c.offset).add(c.spread);
                    return shape(key.value(), c.rect, nullptr);
                },
                [&](const PathCommand& c) {
                    key.add(c, paths_);
                    return shape(key.value(), c.window_bounds(), &c.fill);
                },
                [&](const ImageCommand& c) {
                    key.add(c.rect).add(c.uv).add(c.image ? c.image->id : 0).add(c.opacity);
                    return shape(key.value(), c.rect, nullptr);
                },
                [&](const VideoCommand& c) {
                    // A new frame of its source counts as a change.
                    key.add(c.rect).add(c.uv).add(static_cast<const void*>(c.source.get())).add(c.opacity);
                    key.add(c.content_mode ? static_cast<int>(*c.content_mode) : -1);
                    if (c.source) {
                        if (const std::optional<VideoFrame> frame = c.source->current()) {
                            key.add(frame->source_id).add(frame->frame_id).add(frame->texture_id).add(frame->width)
                                .add(frame->height);
                        }
                    }
                    return shape(key.value(), c.rect, nullptr);
                },
                [&](const PushClipCommand& c) {
                    key.add(c.rect).add(c.radii);
                    Level& level = levels_.back();
                    const Rect clip = intersect(level.clips.back(), pixels(c.rect));
                    const Footprint print{key.value(), up(clip), std::nullopt};
                    level.clips.push_back(clip);
                    level.contexts.push_back(key.value());
                    return print;
                },
                [&](const PopClipCommand&) {
                    Level& level = levels_.back();
                    if (level.clips.size() > 1) {
                        level.clips.pop_back();
                        level.contexts.pop_back();
                    }
                    return Footprint{key.value(), {}, std::nullopt};
                },
                [&](const PushLayerCommand& c) { return push_layer(c, key); },
                [&](const LayerMaskCommand&) { return Footprint{key.value(), {}, std::nullopt}; },
                [&](const PopLayerCommand&) {
                    if (levels_.size() > 1) {
                        levels_.pop_back();
                    }
                    return Footprint{key.value(), {}, std::nullopt};
                },
                [&](const PushScrollCommand& c) {
                    // Its content, in its own points, shows at its origin, within its viewport:
                    // what's drawn there is in its context, which its origin is part of.
                    key.add(c.id).add(c.viewport).add(c.origin);
                    const Point offset{c.origin.x * scale_, c.origin.y * scale_};
                    const Rect viewport = pixels(c.viewport);
                    Level level;
                    level.clips = {Rect{viewport.x - offset.x, viewport.y - offset.y, viewport.width, viewport.height}};
                    level.contexts = {key.value()};
                    level.offset = offset;
                    levels_.push_back(level);
                    return Footprint{key.value(), up(levels_.back().clips.back()), std::nullopt};
                },
                [&](const PopScrollCommand&) {
                    if (levels_.size() > 1) {
                        levels_.pop_back();
                    }
                    return Footprint{key.value(), {}, std::nullopt};
                },
            },
            command);
    }

    Footprint push_layer(const PushLayerCommand& c, Hasher& key) {
        key.add(c.bounds).add(c.opacity).add(c.blur).add(c.blur_opaque).add(c.blend);
        if (c.shadow) {
            key.add(1).add(*c.shadow);
        }
        if (c.inner_shadow) {
            key.add(2).add(*c.inner_shadow);
        }
        if (c.transform) {
            key.add(3).add(*c.transform);
        }
        if (c.color_matrix) {
            key.bytes(c.color_matrix->data(), sizeof(*c.color_matrix));
        }
        if (c.mask) {
            key.add(*c.mask, paths_);
        }

        // Its content is in the window's coordinates before its transform: the clips around it
        // apply once it's placed (up()). What's drawn in it is in its context.
        Level level;
        level.clips = {window_unbounded()};
        level.contexts = {key.value()};
        if (c.transform) {
            level.transform = *c.transform;
            level.transform->pivot = {c.transform->pivot.x * scale_, c.transform->pivot.y * scale_};
        }
        if (c.blur > 0 && !c.blur_opaque) {
            level.blur_reach = blur_reach(c.blur * scale_ / 2);
        }
        if (c.shadow) {
            level.shadow_offset = Point{c.shadow->x * scale_, c.shadow->y * scale_};
            level.shadow_reach = blur_reach(std::max(0.f, c.shadow->radius * scale_ / 2));
        }
        if (c.mask && c.mask->polygons) {
            level.mask = pixels(c.mask->window_bounds()).inset(-1);
        }

        // Where it draws: its content, widened by its effects, placed.
        Footprint print{key.value(), {}, std::nullopt};
        levels_.push_back(level);
        print.bounds = up(pixels(c.bounds).inset(-1));
        if (c.blend != BlendMode::normal) {
            print.reads = print.bounds; // it reads what's behind where it draws
        }
        return print;
    }

    static Rect window_unbounded() {
        constexpr float kFar = 1e7f;
        return {-kFar, -kFar, 2 * kFar, 2 * kFar};
    }

    const DisplayList& list_;
    PathHashes& paths_;
    float scale_;
    Rect window_;
    std::vector<Level> levels_;
};

// Which elements of `a` and `b` are kept, in order, from one to the other: the longest common
// subsequence (Myers' diff). Past `limit` edits it gives up, and marks the middle as changed.
void match(std::span<const Footprint> a, std::span<const Footprint> b, std::vector<bool>& kept_a,
           std::vector<bool>& kept_b) {
    kept_a.assign(a.size(), false);
    kept_b.assign(b.size(), false);
    // The common start and end first: what usually doesn't change.
    std::size_t start = 0;
    while (start < a.size() && start < b.size() && a[start] == b[start]) {
        kept_a[start] = kept_b[start] = true;
        ++start;
    }
    std::size_t end_a = a.size();
    std::size_t end_b = b.size();
    while (end_a > start && end_b > start && a[end_a - 1] == b[end_b - 1]) {
        --end_a;
        --end_b;
        kept_a[end_a] = kept_b[end_b] = true;
    }
    const auto n = static_cast<std::ptrdiff_t>(end_a - start);
    const auto m = static_cast<std::ptrdiff_t>(end_b - start);
    if (n == 0 || m == 0) {
        return;
    }
    constexpr std::ptrdiff_t kLimit = 2048;
    const std::ptrdiff_t max = std::min(n + m, kLimit);
    const std::ptrdiff_t offset = max + 1;
    std::vector<std::ptrdiff_t> v(static_cast<std::size_t>(2 * max + 3), 0);
    std::vector<std::vector<std::ptrdiff_t>> trace;
    const auto equal = [&](std::ptrdiff_t x, std::ptrdiff_t y) {
        return a[start + static_cast<std::size_t>(x)] == b[start + static_cast<std::size_t>(y)];
    };
    std::ptrdiff_t found = -1;
    for (std::ptrdiff_t d = 0; d <= max && found < 0; ++d) {
        trace.push_back(v);
        for (std::ptrdiff_t k = -d; k <= d; k += 2) {
            const auto at = [&](std::ptrdiff_t i) -> std::ptrdiff_t& {
                return v[static_cast<std::size_t>(i + offset)];
            };
            std::ptrdiff_t x = (k == -d || (k != d && at(k - 1) < at(k + 1))) ? at(k + 1) : at(k - 1) + 1;
            std::ptrdiff_t y = x - k;
            while (x < n && y < m && equal(x, y)) {
                ++x;
                ++y;
            }
            at(k) = x;
            if (x >= n && y >= m) {
                found = d;
                break;
            }
        }
    }
    if (found < 0) {
        return; // too many edits: the middle counts as changed
    }
    // Back through the edits, marking the diagonals (kept elements).
    std::ptrdiff_t x = n;
    std::ptrdiff_t y = m;
    for (std::ptrdiff_t d = found; d > 0; --d) {
        const std::vector<std::ptrdiff_t>& before = trace[static_cast<std::size_t>(d)];
        const auto at = [&](std::ptrdiff_t i) { return before[static_cast<std::size_t>(i + offset)]; };
        const std::ptrdiff_t k = x - y;
        const std::ptrdiff_t previous_k = (k == -d || (k != d && at(k - 1) < at(k + 1))) ? k + 1 : k - 1;
        const std::ptrdiff_t previous_x = at(previous_k);
        const std::ptrdiff_t previous_y = previous_x - previous_k;
        while (x > previous_x && y > previous_y) {
            --x;
            --y;
            kept_a[start + static_cast<std::size_t>(x)] = kept_b[start + static_cast<std::size_t>(y)] = true;
        }
        x = previous_x;
        y = previous_y;
    }
    while (x > 0 && y > 0) {
        --x;
        --y;
        kept_a[start + static_cast<std::size_t>(x)] = kept_b[start + static_cast<std::size_t>(y)] = true;
    }
}

} // namespace

std::vector<Footprint> footprints(const DisplayList& list, std::uint32_t width, std::uint32_t height,
                                  PathHashes* paths) {
    PathHashes once;
    return Walker(list, width, height, paths ? *paths : once).run();
}

std::vector<Rect> damage_between(std::span<const Footprint> before, std::span<const Footprint> now) {
    std::vector<bool> kept_before;
    std::vector<bool> kept_now;
    match(before, now, kept_before, kept_now);
    std::vector<Rect> damage;
    for (std::size_t i = 0; i < before.size(); ++i) {
        if (!kept_before[i] && has_area(before[i].bounds)) {
            damage.push_back(before[i].bounds);
        }
    }
    std::vector<bool> added(now.size(), false);
    for (std::size_t i = 0; i < now.size(); ++i) {
        if (!kept_now[i] && has_area(now[i].bounds)) {
            damage.push_back(now[i].bounds);
            added[i] = true;
        }
    }
    // Effects that read what's behind them, where the damage reaches what they read: all they
    // draw, until nothing more is added (ELGSPEC-0029.R5).
    for (bool grew = true; grew;) {
        grew = false;
        for (std::size_t i = 0; i < now.size(); ++i) {
            if (added[i] || !now[i].reads || !has_area(now[i].bounds)) {
                continue;
            }
            if (std::ranges::any_of(damage, [&](const Rect& r) { return overlaps(r, *now[i].reads); })) {
                damage.push_back(now[i].bounds);
                added[i] = true;
                grew = true;
            }
        }
    }
    return damage;
}

std::vector<Rect> drawn_area(std::vector<Rect> damage) {
    std::erase_if(damage, [](const Rect& r) { return !has_area(r); });
    struct Item {
        Rect rect;
        float area; // of the rects it stands for
    };
    std::vector<Item> items;
    items.reserve(damage.size());
    for (const Rect& r : damage) {
        items.push_back({r, area(r)});
    }
    const auto added = [](const Item& a, const Item& b) { return area(union_of(a.rect, b.rect)) - a.area - b.area; };
    const auto merge_best = [&](bool only_cheap) {
        float best = std::numeric_limits<float>::max();
        std::size_t best_i = 0;
        std::size_t best_j = 0;
        for (std::size_t i = 0; i < items.size(); ++i) {
            for (std::size_t j = i + 1; j < items.size(); ++j) {
                const float cost = added(items[i], items[j]);
                if (only_cheap && area(union_of(items[i].rect, items[j].rect)) > 2 * (items[i].area + items[j].area)) {
                    continue;
                }
                if (cost < best) {
                    best = cost;
                    best_i = i;
                    best_j = j;
                }
            }
        }
        if (best == std::numeric_limits<float>::max()) {
            return false;
        }
        items[best_i] = {union_of(items[best_i].rect, items[best_j].rect), items[best_i].area + items[best_j].area};
        items.erase(items.begin() + static_cast<std::ptrdiff_t>(best_j));
        return true;
    };
    // Many rects (a paragraph's glyphs): neighbors in reading order first, where that's cheap.
    if (items.size() > 64) {
        std::ranges::sort(items, [](const Item& a, const Item& b) {
            return a.rect.y != b.rect.y ? a.rect.y < b.rect.y : a.rect.x < b.rect.x;
        });
        std::vector<Item> merged;
        for (const Item& item : items) {
            if (!merged.empty() &&
                area(union_of(merged.back().rect, item.rect)) <= 2 * (merged.back().area + item.area)) {
                merged.back() = {union_of(merged.back().rect, item.rect), merged.back().area + item.area};
            } else {
                merged.push_back(item);
            }
        }
        items = std::move(merged);
    }
    while (merge_best(true)) {
    }
    while (items.size() > 4) {
        merge_best(false);
    }
    std::vector<Rect> out;
    out.reserve(items.size());
    for (const Item& item : items) {
        out.push_back(item.rect);
    }
    return out;
}

} // namespace elg::detail
