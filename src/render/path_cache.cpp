// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include "render/path_cache.h"

#include <algorithm>
#include <cstddef>
#include <vector>

namespace elg::detail {

// Reads a Path's commands, to hash them.
struct PathAccess {
    static std::uint64_t hash(const Path& path, std::uint64_t hash) {
        const auto mix = [&](const void* data, std::size_t size) {
            const auto* bytes = static_cast<const unsigned char*>(data);
            for (std::size_t i = 0; i < size; ++i) {
                hash = (hash ^ bytes[i]) * 1099511628211ull;
            }
        };
        mix(path.verbs_.data(), path.verbs_.size() * sizeof(Path::Verb));
        mix(path.points_.data(), path.points_.size() * sizeof(Point));
        // Where the verbs end and the points begin.
        hash = (hash ^ path.verbs_.size()) * 1099511628211ull;
        return hash;
    }
};

namespace {

thread_local PathCache* t_current = nullptr;

bool same_stroke(const std::optional<StrokeStyle>& a, const StrokeStyle* b) {
    if (!a || !b) {
        return !a && !b;
    }
    return a->width == b->width && a->cap == b->cap && a->join == b->join && a->miter_limit == b->miter_limit &&
           a->dash == b->dash && a->dash_phase == b->dash_phase;
}

std::uint64_t key_of(const Path& path, const StrokeStyle* stroke, float tolerance) {
    std::uint64_t hash = PathAccess::hash(path, 14695981039346656037ull);
    const auto mix = [&](const void* data, std::size_t size) {
        const auto* bytes = static_cast<const unsigned char*>(data);
        for (std::size_t i = 0; i < size; ++i) {
            hash = (hash ^ bytes[i]) * 1099511628211ull;
        }
    };
    mix(&tolerance, sizeof(tolerance));
    if (stroke) {
        const float numbers[] = {stroke->width, stroke->miter_limit, stroke->dash_phase,
                                 static_cast<float>(stroke->cap), static_cast<float>(stroke->join)};
        mix(numbers, sizeof(numbers));
        mix(stroke->dash.data(), stroke->dash.size() * sizeof(float));
    }
    return hash;
}

} // namespace

PathCache::Use::Use(PathCache& cache) noexcept : previous_(t_current) {
    t_current = &cache;
}

PathCache::Use::~Use() {
    t_current = previous_;
}

PathCache* PathCache::current() noexcept {
    return t_current;
}

PathCache::Flattened PathCache::flatten(const Path& path, const StrokeStyle* stroke, float tolerance) {
    auto polygons = std::make_shared<Polygons>();
    bool any = false;
    float x0 = 0;
    float y0 = 0;
    float x1 = 0;
    float y1 = 0;
    for (Path::Polyline& line : (stroke ? path.stroked(*stroke, tolerance) : path).flatten(tolerance)) {
        if (line.points.size() < 3) {
            continue;
        }
        for (const Point p : line.points) {
            x0 = any ? std::min(x0, p.x) : p.x;
            y0 = any ? std::min(y0, p.y) : p.y;
            x1 = any ? std::max(x1, p.x) : p.x;
            y1 = any ? std::max(y1, p.y) : p.y;
            any = true;
        }
        polygons->push_back(std::move(line.points));
    }
    return {std::move(polygons), {x0, y0, x1 - x0, y1 - y0}};
}

PathCache::Flattened PathCache::get(const Path& path, const StrokeStyle* stroke, float tolerance) {
    const std::uint64_t key = key_of(path, stroke, tolerance);
    const auto [first, last] = entries_.equal_range(key);
    for (auto it = first; it != last; ++it) {
        Entry& entry = it->second;
        if (entry.tolerance == tolerance && same_stroke(entry.stroke, stroke) && entry.path == path) {
            entry.used = true;
            return entry.flattened;
        }
    }
    ++flattened_;
    Entry entry{path, stroke ? std::optional(*stroke) : std::nullopt, tolerance, flatten(path, stroke, tolerance),
                true};
    return entries_.emplace(key, std::move(entry))->second.flattened;
}

void PathCache::end_frame() {
    std::erase_if(entries_, [](const auto& entry) { return !entry.second.used; });
    for (auto& [key, entry] : entries_) {
        entry.used = false;
    }
}

} // namespace elg::detail
