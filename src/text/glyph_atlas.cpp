// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include "text/glyph_atlas.h"

#include "core/log.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H

#ifdef _MSC_VER
#pragma warning(push, 0)
#endif
#include <msdfgen.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace elg::detail {

namespace {

// Builds an msdfgen shape from a FreeType outline (font units, y up).
struct OutlineBuilder {
    msdfgen::Shape* shape;
    msdfgen::Contour* contour = nullptr;
    msdfgen::Point2 position{};

    static msdfgen::Point2 point(const FT_Vector* v) {
        return {static_cast<double>(v->x), static_cast<double>(v->y)};
    }

    static int move_to(const FT_Vector* to, void* user) {
        auto* self = static_cast<OutlineBuilder*>(user);
        if (!(self->contour && self->contour->edges.empty())) {
            self->contour = &self->shape->addContour();
        }
        self->position = point(to);
        return 0;
    }

    static int line_to(const FT_Vector* to, void* user) {
        auto* self = static_cast<OutlineBuilder*>(user);
        const msdfgen::Point2 end = point(to);
        if (end != self->position) {
            self->contour->addEdge(msdfgen::EdgeHolder(self->position, end));
            self->position = end;
        }
        return 0;
    }

    static int conic_to(const FT_Vector* control, const FT_Vector* to, void* user) {
        auto* self = static_cast<OutlineBuilder*>(user);
        const msdfgen::Point2 end = point(to);
        if (end != self->position) {
            self->contour->addEdge(msdfgen::EdgeHolder(self->position, point(control), end));
            self->position = end;
        }
        return 0;
    }

    static int cubic_to(const FT_Vector* control1, const FT_Vector* control2, const FT_Vector* to, void* user) {
        auto* self = static_cast<OutlineBuilder*>(user);
        const msdfgen::Point2 end = point(to);
        if (end != self->position) {
            self->contour->addEdge(msdfgen::EdgeHolder(self->position, point(control1), point(control2), end));
            self->position = end;
        }
        return 0;
    }
};

std::uint8_t to_byte(float value) {
    return static_cast<std::uint8_t>(std::clamp(value * 255.f + 0.5f, 0.f, 255.f));
}

float median(float a, float b, float c) {
    return std::max(std::min(a, b), std::min(std::max(a, b), c));
}

// Whether the shape's contours overlap: somewhere the winding number passes 1. Variable fonts
// build glyphs that way (a stroke drawn across a bowl, the arms of a y). Along each row the winding
// is exact between crossings, so an overlap narrower than a field pixel isn't missed; rows are
// kRowsPerPixel to a field pixel, so neither is one barely taller than that.
bool overlaps(const msdfgen::Shape& shape, const msdfgen::Projection& projection, int rows) {
    constexpr int kRowsPerPixel = 8;
    std::vector<std::pair<double, int>> crossings; // x, direction
    for (int row = 0; row < rows * kRowsPerPixel; ++row) {
        const double y = projection.unprojectY((row + 0.5) / kRowsPerPixel);
        crossings.clear();
        for (const msdfgen::Contour& contour : shape.contours) {
            for (const msdfgen::EdgeHolder& edge : contour.edges) {
                double x[3];
                int direction[3];
                const int count = edge->scanlineIntersections(x, direction, y);
                for (int i = 0; i < count; ++i) {
                    crossings.emplace_back(x[i], direction[i]);
                }
            }
        }
        std::ranges::sort(crossings);
        int winding = 0;
        for (const auto& [x, direction] : crossings) {
            winding += direction;
            if (std::abs(winding) > 1) {
                return true;
            }
        }
    }
    return false;
}

// Where pixel (x, y) is in a row-major bitmap `width` pixels wide, and how many pixels one has.
std::size_t pixel_index(int x, int y, int width) {
    return static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
}

std::size_t pixel_count(int width, int height) {
    return static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
}

// One row or column of a squared Euclidean distance transform (Felzenszwalb and Huttenlocher):
// d[q] = min over p of (q - p)^2 + f[p]. `v` and `z` are scratch of n and n + 1.
void distance_transform(const float* f, float* d, int n, std::vector<int>& v, std::vector<float>& z) {
    constexpr float kInfinity = 1e20f;
    int k = 0;
    v[0] = 0;
    z[0] = -kInfinity;
    z[1] = kInfinity;
    for (int q = 1; q < n; ++q) {
        float s = 0;
        while (true) {
            const auto p = static_cast<float>(v[static_cast<std::size_t>(k)]);
            s = ((f[q] + static_cast<float>(q) * static_cast<float>(q)) - (f[v[static_cast<std::size_t>(k)]] + p * p)) /
                (2.f * static_cast<float>(q) - 2.f * p);
            if (s > z[static_cast<std::size_t>(k)] || k == 0) {
                break;
            }
            --k;
        }
        ++k;
        v[static_cast<std::size_t>(k)] = q;
        z[static_cast<std::size_t>(k)] = s;
        z[static_cast<std::size_t>(k) + 1] = kInfinity;
    }
    k = 0;
    for (int q = 0; q < n; ++q) {
        while (z[static_cast<std::size_t>(k) + 1] < static_cast<float>(q)) {
            ++k;
        }
        const int p = v[static_cast<std::size_t>(k)];
        d[q] = static_cast<float>((q - p) * (q - p)) + f[p];
    }
}

// Squared distance from every pixel to the nearest one where `target` is set.
std::vector<float> squared_distances(const std::vector<std::uint8_t>& target, int width, int height) {
    std::vector<float> grid(target.size());
    for (std::size_t i = 0; i < grid.size(); ++i) {
        grid[i] = target[i] ? 0.f : 1e20f;
    }
    const auto n = static_cast<std::size_t>(std::max(width, height));
    std::vector<float> line(n);
    std::vector<float> result(n);
    std::vector<float> z(n + 1);
    std::vector<int> v(n);
    const auto at = [&](int x, int y) -> float& { return grid[pixel_index(x, y, width)]; };
    for (int x = 0; x < width; ++x) {
        for (int y = 0; y < height; ++y) {
            line[static_cast<std::size_t>(y)] = at(x, y);
        }
        distance_transform(line.data(), result.data(), height, v, z);
        for (int y = 0; y < height; ++y) {
            at(x, y) = result[static_cast<std::size_t>(y)];
        }
    }
    for (int y = 0; y < height; ++y) {
        distance_transform(&at(0, y), result.data(), width, v, z);
        std::copy_n(result.begin(), width, &at(0, y));
    }
    return grid;
}

// The exact signed distance at each field pixel, as the field stores it (0.5 at the edge, rows
// bottom first): from the shape filled with the non-zero rule (so where contours overlap, their
// inner edges don't count) at kOversample times the resolution.
std::vector<float> exact_distances(const msdfgen::Shape& shape, double scale, const msdfgen::Vector2& translate,
                                   int width, int height) {
    constexpr int kOversample = 8;
    const int fine_width = width * kOversample;
    const int fine_height = height * kOversample;
    msdfgen::Bitmap<float, 1> fill(fine_width, fine_height);
    msdfgen::rasterize(fill, shape, msdfgen::Projection(msdfgen::Vector2(scale * kOversample), translate),
                       msdfgen::FILL_NONZERO);
    std::vector<std::uint8_t> inside(pixel_count(fine_width, fine_height));
    std::vector<std::uint8_t> outside(inside.size());
    for (int y = 0; y < fine_height; ++y) {
        for (int x = 0; x < fine_width; ++x) {
            const std::size_t i = pixel_index(x, y, fine_width);
            inside[i] = *fill(x, y) > 0.5f;
            outside[i] = !inside[i];
        }
    }
    const std::vector<float> to_inside = squared_distances(inside, fine_width, fine_height);
    const std::vector<float> to_outside = squared_distances(outside, fine_width, fine_height);

    std::vector<float> distances(pixel_count(width, height));
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            // A field pixel's center lies between four fine pixels: their average.
            double sum = 0;
            for (int dy = 0; dy < 2; ++dy) {
                for (int dx = 0; dx < 2; ++dx) {
                    const std::size_t i = pixel_index(x * kOversample + kOversample / 2 - 1 + dx,
                                                      y * kOversample + kOversample / 2 - 1 + dy, fine_width);
                    // From pixel centers to the boundary between pixels: half a pixel less.
                    sum += inside[i] ? std::sqrt(static_cast<double>(to_outside[i])) - 0.5
                                     : -(std::sqrt(static_cast<double>(to_inside[i])) - 0.5);
                }
            }
            const double pixels = sum / 4 / kOversample; // field pixels, positive inside
            distances[pixel_index(x, y, width)] = static_cast<float>(pixels / static_cast<double>(kDistanceRange) + 0.5);
        }
    }
    return distances;
}

// A multi-channel field of overlapping contours goes wrong along their inner edges: its sign
// (fixed by scanning the fill) and its distances (fixed here). Where its median strays from the
// exact distance, and a little around, the exact distance replaces it; elsewhere (the outline's
// own edges and corners) the multi-channel field keeps its sharp corners.
void resolve_overlaps(msdfgen::Bitmap<float, 4>& field, const msdfgen::Shape& shape,
                      const msdfgen::Projection& projection, double scale, const msdfgen::Vector2& translate) {
    const int width = field.width();
    const int height = field.height();
    msdfgen::distanceSignCorrection(field, shape, projection, 0.5f, msdfgen::FILL_NONZERO);
    const std::vector<float> exact = exact_distances(shape, scale, translate, width, height);

    constexpr float kTolerance = 0.25f; // pixels
    constexpr int kAround = 2;          // pixels around a stray one also replaced (no seams between)
    std::vector<std::uint8_t> stray(exact.size());
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const float* pixel = field(x, y);
            const std::size_t i = pixel_index(x, y, width);
            stray[i] = std::abs(median(pixel[0], pixel[1], pixel[2]) - exact[i]) * kDistanceRange > kTolerance;
        }
    }
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            bool replace = false;
            for (int ny = std::max(y - kAround, 0); ny <= std::min(y + kAround, height - 1) && !replace; ++ny) {
                for (int nx = std::max(x - kAround, 0); nx <= std::min(x + kAround, width - 1) && !replace; ++nx) {
                    replace = stray[pixel_index(nx, ny, width)] != 0;
                }
            }
            float* pixel = field(x, y);
            const float distance = exact[pixel_index(x, y, width)];
            if (replace) {
                pixel[0] = pixel[1] = pixel[2] = distance;
            }
            pixel[3] = distance; // the true distance channel, without the inner edges
        }
    }
}

} // namespace

const AtlasGlyph* GlyphAtlas::find_or_add(const FontFace& face, std::uint32_t glyph_id) {
    const std::uint64_t key = (static_cast<std::uint64_t>(face.id()) << 32) | glyph_id;
    auto it = glyphs_.find(key);
    if (it == glyphs_.end()) {
        it = glyphs_.emplace(key, prepare(face, glyph_id)).first;
    }
    return it->second ? &*it->second : nullptr;
}

// A glyph's distance field to generate: everything a worker needs, independent of FreeType.
struct GlyphJob {
    msdfgen::Shape shape;
    const FontFace* face = nullptr; // for the disk cache (fonts outlive the atlas)
    std::uint32_t glyph = 0;
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    double scale = 1;
    msdfgen::Vector2 translate;
    bool icon = false;
};

namespace {

GlyphUpload generate(const GlyphJob& job) {
    msdfgen::Bitmap<float, 4> bitmap(static_cast<int>(job.width), static_cast<int>(job.height));
    const msdfgen::Projection projection(msdfgen::Vector2(job.scale), job.translate);
    const msdfgen::SDFTransformation transformation(projection,
                                                    msdfgen::DistanceMapping(msdfgen::Range(static_cast<double>(kDistanceRange) / job.scale)));
    if (job.icon) {
        // Icon fonts build glyphs from overlapping outlines (strokes, rings, teeth), which a
        // multi-channel field turns into speckles and streaks. A true single-channel distance
        // handles overlaps cleanly; its slightly rounder corners don't show at icon sizes. Every
        // channel gets it, so the shader's median is the same distance.
        msdfgen::Bitmap<float, 1> sdf(static_cast<int>(job.width), static_cast<int>(job.height));
        msdfgen::generateSDF(sdf, job.shape, transformation, msdfgen::GeneratorConfig(true));
        // Where outlines overlap, inside vs. outside is decided by scanning the shape with the
        // non-zero fill rule, exactly as the font is meant to be filled. (Safe with one channel;
        // with three it leaves the channels disagreeing.)
        msdfgen::distanceSignCorrection(sdf, job.shape, projection, msdfgen::FILL_NONZERO);
        for (int row = 0; row < static_cast<int>(job.height); ++row) {
            for (int column = 0; column < static_cast<int>(job.width); ++column) {
                float* pixel = bitmap(column, row);
                pixel[0] = pixel[1] = pixel[2] = pixel[3] = *sdf(column, row);
            }
        }
    } else {
        msdfgen::generateMTSDF(bitmap, job.shape, transformation);
        if (overlaps(job.shape, projection, static_cast<int>(job.height))) {
            resolve_overlaps(bitmap, job.shape, projection, job.scale, job.translate);
        }
    }

    // msdfgen bitmaps are bottom row first; the atlas is top row first.
    GlyphUpload upload{job.x, job.y, job.width, job.height, std::vector<std::uint8_t>(std::size_t{job.width} * job.height * 4)};
    for (std::uint32_t row = 0; row < job.height; ++row) {
        for (std::uint32_t column = 0; column < job.width; ++column) {
            const float* pixel = bitmap(static_cast<int>(column), static_cast<int>(job.height - 1 - row));
            std::uint8_t* out = &upload.rgba[(std::size_t{row} * job.width + column) * 4];
            for (int channel = 0; channel < 4; ++channel) {
                out[channel] = to_byte(pixel[channel]);
            }
        }
    }
    return upload;
}

} // namespace

GlyphAtlas::GlyphAtlas() = default;

GlyphAtlas::~GlyphAtlas() {
    {
        // Glyphs not started yet are no longer wanted.
        std::lock_guard lock(mutex_);
        pending_ -= jobs_.size();
        jobs_.clear();
    }
    for (std::jthread& worker : workers_) {
        worker.request_stop();
    }
    wake_workers_.notify_all();
    workers_.clear(); // joins
}

void GlyphAtlas::submit(std::unique_ptr<GlyphJob> job) {
    ++requested_;
    {
        std::lock_guard lock(mutex_);
        jobs_.push_back(std::move(job));
        ++pending_;
        if (workers_.empty()) {
            // Leave a core for the UI and render threads. Debug builds use fewer: the debug heap is
            // one lock, and msdfgen allocates constantly, so more threads only queue on it
            // (measured: 3 workers finish a screenful about 2.5x faster than 8).
#if defined(NDEBUG)
            constexpr unsigned kMaxWorkers = 8;
#else
            constexpr unsigned kMaxWorkers = 3;
#endif
            const unsigned count = std::clamp(std::thread::hardware_concurrency(), 2u, kMaxWorkers + 1) - 1;
            for (unsigned i = 0; i < count; ++i) {
                workers_.emplace_back([this](std::stop_token stop) { work(stop); });
            }
        }
    }
    wake_workers_.notify_one();
}

void GlyphAtlas::work(std::stop_token stop) {
    while (true) {
        std::unique_ptr<GlyphJob> job;
        {
            std::unique_lock lock(mutex_);
            if (!wake_workers_.wait(lock, stop, [&] { return !jobs_.empty(); }) || stop.stop_requested()) {
                return; // stopping
            }
            job = std::move(jobs_.front());
            jobs_.pop_front();
        }
        GlyphUpload upload = generate(*job);
        if (disk_) {
            disk_->store(*job->face, job->glyph, upload.width, upload.height, upload.rgba);
        }
        std::function<void()> on_ready;
        {
            std::lock_guard lock(mutex_);
            uploads_.push_back(std::move(upload));
            --pending_;
            on_ready = on_ready_;
        }
        idle_.notify_all();
        if (on_ready) {
            on_ready();
        }
    }
}

std::vector<GlyphUpload> GlyphAtlas::take_uploads() {
    std::lock_guard lock(mutex_);
    return std::exchange(uploads_, {});
}

bool GlyphAtlas::has_uploads() const {
    std::lock_guard lock(mutex_);
    return !uploads_.empty();
}

void GlyphAtlas::set_on_ready(std::function<void()> on_ready) {
    std::lock_guard lock(mutex_);
    on_ready_ = std::move(on_ready);
}

void GlyphAtlas::set_disk_cache(std::optional<std::filesystem::path> directory) {
    disk_ = directory ? std::make_unique<GlyphDiskCache>(std::move(directory)) : nullptr;
    if (disk_ && !disk_->enabled()) {
        disk_.reset();
    }
}

bool GlyphAtlas::wait_idle(std::chrono::milliseconds timeout) {
    std::unique_lock lock(mutex_);
    return idle_.wait_for(lock, timeout, [&] { return pending_ == 0; });
}

bool GlyphAtlas::allocate(std::uint32_t width, std::uint32_t height, std::uint32_t& x, std::uint32_t& y) {
    // Simple shelf packing with a 1px gap between glyphs.
    if (shelf_x_ + width + 1 > kAtlasSize) {
        shelf_y_ += shelf_height_;
        shelf_x_ = 0;
        shelf_height_ = 0;
    }
    if (width + 1 > kAtlasSize || shelf_y_ + height + 1 > kAtlasSize) {
        return false;
    }
    x = shelf_x_ + 1;
    y = shelf_y_ + 1;
    shelf_x_ += width + 1;
    shelf_height_ = std::max(shelf_height_, height + 1);
    return true;
}

std::optional<AtlasGlyph> GlyphAtlas::prepare(const FontFace& face, std::uint32_t glyph_id) {
    FT_Face ft = face.ft_face();
    if (FT_Load_Glyph(ft, glyph_id, FT_LOAD_NO_SCALE | FT_LOAD_NO_BITMAP | FT_LOAD_NO_HINTING) != 0) {
        log_warning("failed to load glyph {} of font {}", glyph_id, face.id());
        return std::nullopt;
    }
    if (ft->glyph->outline.n_contours <= 0) {
        return std::nullopt; // e.g. a space
    }

    msdfgen::Shape shape;
    OutlineBuilder builder{&shape};
    FT_Outline_Funcs funcs{};
    funcs.move_to = &OutlineBuilder::move_to;
    funcs.line_to = &OutlineBuilder::line_to;
    funcs.conic_to = &OutlineBuilder::conic_to;
    funcs.cubic_to = &OutlineBuilder::cubic_to;
    if (FT_Outline_Decompose(&ft->glyph->outline, &funcs, &builder) != 0) {
        log_warning("failed to read the outline of glyph {} of font {}", glyph_id, face.id());
        return std::nullopt;
    }
    if (!shape.contours.empty() && shape.contours.back().edges.empty()) {
        shape.contours.pop_back();
    }
    // msdfgen takes filled areas from contour direction, in TrueType's convention (outer contours
    // clockwise). Fonts drawn the other way (PostScript-style, common in icon fonts converted from
    // SVG) would come out inside-out, so turn them around first.
    if (FT_Outline_Get_Orientation(&ft->glyph->outline) == FT_ORIENTATION_POSTSCRIPT) {
        for (msdfgen::Contour& contour : shape.contours) {
            contour.reverse();
        }
    }
    shape.normalize();
    if (shape.contours.empty() || !shape.validate()) {
        return std::nullopt;
    }
    msdfgen::edgeColoringSimple(shape, 3.0);

    // Size the bitmap to the glyph at kGlyphEmPixels, plus room for the distance field.
    const msdfgen::Shape::Bounds bounds = shape.getBounds();
    const double scale = static_cast<double>(kGlyphEmPixels / face.units_per_em()); // pixels per font unit
    const double pad = static_cast<double>(kDistanceRange) / 2.0 + 1.0;             // pixels
    const auto width = static_cast<std::uint32_t>(std::ceil((bounds.r - bounds.l) * scale + 2 * pad));
    const auto height = static_cast<std::uint32_t>(std::ceil((bounds.t - bounds.b) * scale + 2 * pad));

    std::uint32_t x = 0;
    std::uint32_t y = 0;
    if (!allocate(width, height, x, y)) {
        if (!reported_full_) {
            log_warning("the glyph atlas is full; further new glyphs won't be drawn");
            reported_full_ = true;
        }
        return std::nullopt;
    }

    // Generated in an earlier run: the pixels come straight from the disk cache. Otherwise the
    // distance field is generated on a worker; until then the slot is empty.
    std::optional<std::vector<std::uint8_t>> cached;
    if (disk_) {
        cached = disk_->find(face, glyph_id, width, height);
    }
    if (cached) {
        std::lock_guard lock(mutex_);
        uploads_.push_back({x, y, width, height, std::move(*cached)});
    } else {
    auto job = std::make_unique<GlyphJob>();
    job->x = x;
    job->y = y;
    job->width = width;
    job->height = height;
    job->scale = scale;
    job->translate = msdfgen::Vector2(-bounds.l + pad / scale, -bounds.b + pad / scale);
    job->icon = face.icon() || face.has_color(); // icon and color-layer glyphs overlap themselves
    job->shape = std::move(shape);
    job->face = &face;
    job->glyph = glyph_id;
    submit(std::move(job));
    }

    const float em = face.units_per_em();
    const auto pad_units = static_cast<float>(pad / scale);
    AtlasGlyph glyph;
    glyph.u0 = static_cast<float>(x) / kAtlasSize;
    glyph.v0 = static_cast<float>(y) / kAtlasSize;
    glyph.u1 = static_cast<float>(x + width) / kAtlasSize;
    glyph.v1 = static_cast<float>(y + height) / kAtlasSize;
    glyph.left = (static_cast<float>(bounds.l) - pad_units) / em;
    glyph.bottom = (static_cast<float>(bounds.b) - pad_units) / em;
    glyph.right = glyph.left + static_cast<float>(width / scale) / em;
    glyph.top = glyph.bottom + static_cast<float>(height / scale) / em;
    return glyph;
}

} // namespace elg::detail
