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
        it = glyphs_.emplace(key, generate(face, glyph_id)).first;
    }
    return it->second ? &*it->second : nullptr;
}

std::vector<GlyphUpload> GlyphAtlas::take_uploads() {
    return std::exchange(uploads_, {});
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

std::optional<AtlasGlyph> GlyphAtlas::generate(const FontFace& face, std::uint32_t glyph_id) {
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

    msdfgen::Bitmap<float, 4> bitmap(static_cast<int>(width), static_cast<int>(height));
    const msdfgen::Vector2 translate(-bounds.l + pad / scale, -bounds.b + pad / scale);
    const msdfgen::SDFTransformation transformation(msdfgen::Projection(msdfgen::Vector2(scale), translate),
                                                    msdfgen::DistanceMapping(msdfgen::Range(kDistanceRange / scale)));
    msdfgen::generateMTSDF(bitmap, shape, transformation);

    // msdfgen bitmaps are bottom row first; the atlas is top row first.
    GlyphUpload upload{x, y, width, height, std::vector<std::uint8_t>(std::size_t{width} * height * 4)};
    for (std::uint32_t row = 0; row < height; ++row) {
        for (std::uint32_t column = 0; column < width; ++column) {
            const float* pixel = bitmap(static_cast<int>(column), static_cast<int>(height - 1 - row));
            std::uint8_t* out = &upload.rgba[(std::size_t{row} * width + column) * 4];
            for (int channel = 0; channel < 4; ++channel) {
                out[channel] = to_byte(pixel[channel]);
            }
        }
    }
    uploads_.push_back(std::move(upload));

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
