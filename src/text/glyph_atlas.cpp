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

namespace elg::detail {

namespace {

// Builds an msdfgen shape from a FreeType outline (font units, y up).
struct OutlineBuilder {
    msdfgen::Shape* shape;
    msdfgen::Contour* contour = nullptr;
    msdfgen::Point2 position;

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
                                                    msdfgen::DistanceMapping(msdfgen::Range(kDistanceRange / job.scale)));
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
    const double scale = kGlyphEmPixels / face.units_per_em(); // pixels per font unit
    const double pad = kDistanceRange / 2.0 + 1.0;            // pixels
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

    // The distance field is generated on a worker; until then the slot is empty.
    auto job = std::make_unique<GlyphJob>();
    job->x = x;
    job->y = y;
    job->width = width;
    job->height = height;
    job->scale = scale;
    job->translate = msdfgen::Vector2(-bounds.l + pad / scale, -bounds.b + pad / scale);
    job->icon = face.icon();
    job->shape = std::move(shape);
    submit(std::move(job));

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
