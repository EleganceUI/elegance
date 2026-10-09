// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// COLRv1 color glyphs (design.md §11): FreeType reads the paint graph; it's flattened here into
// filled regions that draw through the GPU path pipeline.

#include "text/font_library.h"

#include <elg/path.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_COLOR_H
#include FT_OUTLINE_H

#include <algorithm>
#include <cmath>
#include <numbers>

namespace elg::detail {

namespace {

// A 2x3 affine transform in font units (y up): x' = xx*x + xy*y + dx, y' = yx*x + yy*y + dy.
struct Affine {
    double xx = 1, xy = 0, dx = 0;
    double yx = 0, yy = 1, dy = 0;

    // This transform after `inner` (inner applies first).
    Affine operator*(const Affine& inner) const {
        return {xx * inner.xx + xy * inner.yx, xx * inner.xy + xy * inner.yy, xx * inner.dx + xy * inner.dy + dx,
                yx * inner.xx + yy * inner.yx, yx * inner.xy + yy * inner.yy, yx * inner.dx + yy * inner.dy + dy};
    }
    void apply(double x, double y, double& ox, double& oy) const {
        ox = xx * x + xy * y + dx;
        oy = yx * x + yy * y + dy;
    }
    // The transform's average scale (for radii).
    double scale() const { return std::sqrt(std::abs(xx * yy - xy * yx)); }

    static Affine translate(double x, double y) { return {1, 0, x, 0, 1, y}; }
    // `m` about the point (cx, cy).
    static Affine around(const Affine& m, double cx, double cy) { return translate(cx, cy) * m * translate(-cx, -cy); }
};

double fixed(FT_Fixed v) {
    return static_cast<double>(v) / 65536.0;
}

constexpr int kMaxDepth = 24;
constexpr std::size_t kMaxPaths = 512;

class PaintWalker {
public:
    PaintWalker(FT_Face face, std::span<const Color> palette, float units_per_em, std::vector<ColorPath>& out)
        : face_(face), palette_(palette), em_(1.0 / units_per_em), out_(out) {}

    void walk(FT_OpaquePaint opaque, const Affine& m, int depth) {
        FT_COLR_Paint paint{};
        if (depth > kMaxDepth || out_.size() >= kMaxPaths || !FT_Get_Paint(face_, opaque, &paint)) {
            return;
        }
        switch (paint.format) {
        case FT_COLR_PAINTFORMAT_COLR_LAYERS: {
            FT_OpaquePaint layer{nullptr, 1};
            FT_LayerIterator iterator = paint.u.colr_layers.layer_iterator;
            while (FT_Get_Paint_Layers(face_, &iterator, &layer)) {
                walk(layer, m, depth + 1);
            }
            break;
        }
        case FT_COLR_PAINTFORMAT_GLYPH: fill_glyph(paint.u.glyph.glyphID, paint.u.glyph.paint, m, depth); break;
        case FT_COLR_PAINTFORMAT_COLR_GLYPH: {
            FT_OpaquePaint root{nullptr, 1};
            if (FT_Get_Color_Glyph_Paint(face_, paint.u.colr_glyph.glyphID, FT_COLOR_NO_ROOT_TRANSFORM, &root)) {
                walk(root, m, depth + 1);
            }
            break;
        }
        case FT_COLR_PAINTFORMAT_COMPOSITE:
            // Blend modes aren't supported: the source draws over the backdrop.
            walk(paint.u.composite.backdrop_paint, m, depth + 1);
            walk(paint.u.composite.source_paint, m, depth + 1);
            break;
        default: {
            // A transform around a subtree, or a fill with no glyph to fill (ignored).
            Affine t;
            FT_OpaquePaint child{};
            if (transform_of(paint, t, child)) {
                walk(child, m * t, depth + 1);
            }
            break;
        }
        }
    }

private:
    // PaintTransform and friends: the transform and the paint it applies to.
    static bool transform_of(const FT_COLR_Paint& paint, Affine& t, FT_OpaquePaint& child) {
        switch (paint.format) {
        case FT_COLR_PAINTFORMAT_TRANSFORM: {
            const FT_Affine23& a = paint.u.transform.affine;
            t = {fixed(a.xx), fixed(a.xy), fixed(a.dx), fixed(a.yx), fixed(a.yy), fixed(a.dy)};
            child = paint.u.transform.paint;
            return true;
        }
        case FT_COLR_PAINTFORMAT_TRANSLATE:
            t = Affine::translate(fixed(paint.u.translate.dx), fixed(paint.u.translate.dy));
            child = paint.u.translate.paint;
            return true;
        case FT_COLR_PAINTFORMAT_SCALE: {
            const FT_PaintScale& s = paint.u.scale;
            t = Affine::around({fixed(s.scale_x), 0, 0, 0, fixed(s.scale_y), 0}, fixed(s.center_x), fixed(s.center_y));
            child = s.paint;
            return true;
        }
        case FT_COLR_PAINTFORMAT_ROTATE: {
            const FT_PaintRotate& r = paint.u.rotate;
            const double angle = fixed(r.angle) * std::numbers::pi; // counter-clockwise, y up
            const double c = std::cos(angle), s = std::sin(angle);
            t = Affine::around({c, -s, 0, s, c, 0}, fixed(r.center_x), fixed(r.center_y));
            child = r.paint;
            return true;
        }
        case FT_COLR_PAINTFORMAT_SKEW: {
            const FT_PaintSkew& k = paint.u.skew;
            const double x = std::tan(-fixed(k.x_skew_angle) * std::numbers::pi);
            const double y = std::tan(fixed(k.y_skew_angle) * std::numbers::pi);
            t = Affine::around({1, x, 0, y, 1, 0}, fixed(k.center_x), fixed(k.center_y));
            child = k.paint;
            return true;
        }
        default: return false;
        }
    }

    // PaintGlyph: the glyph's outline (under `m`) filled with `fill` (whose own transforms apply
    // to the paint only).
    void fill_glyph(FT_UInt glyph, FT_OpaquePaint fill, const Affine& m, int depth) {
        ColorPaint paint;
        if (!resolve_fill(fill, m, paint, depth + 1)) {
            return;
        }
        if (FT_Load_Glyph(face_, glyph, FT_LOAD_NO_SCALE | FT_LOAD_NO_BITMAP | FT_LOAD_NO_HINTING) != 0 ||
            face_->glyph->format != FT_GLYPH_FORMAT_OUTLINE) {
            return;
        }
        Outline outline{this, &m, {}};
        FT_Outline_Funcs funcs{};
        funcs.move_to = [](const FT_Vector* p, void* user) {
            auto& o = *static_cast<Outline*>(user);
            o.path.move_to(o.point(p));
            return 0;
        };
        funcs.line_to = [](const FT_Vector* p, void* user) {
            auto& o = *static_cast<Outline*>(user);
            o.path.line_to(o.point(p));
            return 0;
        };
        funcs.conic_to = [](const FT_Vector* c, const FT_Vector* p, void* user) {
            auto& o = *static_cast<Outline*>(user);
            o.path.quad_to(o.point(c), o.point(p));
            return 0;
        };
        funcs.cubic_to = [](const FT_Vector* c1, const FT_Vector* c2, const FT_Vector* p, void* user) {
            auto& o = *static_cast<Outline*>(user);
            o.path.cubic_to(o.point(c1), o.point(c2), o.point(p));
            return 0;
        };
        if (FT_Outline_Decompose(&face_->glyph->outline, &funcs, &outline) != 0) {
            return;
        }

        auto polygons = std::make_shared<std::vector<std::vector<Point>>>();
        float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        bool any = false;
        for (Path::Polyline& line : outline.path.flatten(1.f / 1024)) {
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
        if (polygons->empty() || x1 <= x0 || y1 <= y0) {
            return;
        }
        out_.push_back({std::move(polygons), {x0, y0, x1 - x0, y1 - y0}, std::move(paint)});
    }

    struct Outline {
        const PaintWalker* walker;
        const Affine* m;
        Path path;
        Point point(const FT_Vector* v) const { return walker->to_em(*m, static_cast<double>(v->x), static_cast<double>(v->y)); }
    };

    // Font units (y up) under `m` to em units (y down).
    Point to_em(const Affine& m, double x, double y) const {
        double ox = 0, oy = 0;
        m.apply(x, y, ox, oy);
        return {static_cast<float>(ox * em_), static_cast<float>(-oy * em_)};
    }

    bool color_of(const FT_ColorIndex& index, Color& color, bool& foreground) const {
        const float alpha = static_cast<float>(index.alpha) / 16384.f;
        foreground = index.palette_index == 0xFFFF || index.palette_index >= palette_.size();
        color = foreground ? Color{0, 0, 0, alpha} : palette_[index.palette_index];
        if (!foreground) {
            color.a *= alpha;
        }
        return true;
    }

    std::vector<ColorStop> stops_of(FT_ColorLine line) const {
        std::vector<ColorStop> stops;
        FT_ColorStop stop{};
        while (FT_Get_Colorline_Stops(face_, &stop, &line.color_stop_iterator)) {
            ColorStop s;
            s.offset = static_cast<float>(fixed(stop.stop_offset));
            color_of(stop.color, s.color, s.foreground);
            stops.push_back(s);
        }
        // Stops outside 0...1 (allowed by COLRv1) are clamped; the gradient pads.
        for (ColorStop& s : stops) {
            s.offset = std::clamp(s.offset, 0.f, 1.f);
        }
        return stops;
    }

    // The fill under a PaintGlyph: a solid or a gradient, possibly under transforms.
    bool resolve_fill(FT_OpaquePaint opaque, const Affine& m, ColorPaint& out, int depth) const {
        FT_COLR_Paint paint{};
        if (depth > kMaxDepth || !FT_Get_Paint(face_, opaque, &paint)) {
            return false;
        }
        switch (paint.format) {
        case FT_COLR_PAINTFORMAT_SOLID:
            out.kind = ColorPaint::Kind::solid;
            return color_of(paint.u.solid.color, out.color, out.foreground);
        case FT_COLR_PAINTFORMAT_LINEAR_GRADIENT: {
            const FT_PaintLinearGradient& g = paint.u.linear_gradient;
            out.kind = ColorPaint::Kind::linear;
            out.stops = stops_of(g.colorline);
            // The gradient runs from p0 toward p1, along the normal of the line p0-p2.
            const double x0 = fixed(g.p0.x), y0 = fixed(g.p0.y);
            double x1 = fixed(g.p1.x), y1 = fixed(g.p1.y);
            const double nx = -(fixed(g.p2.y) - y0), ny = fixed(g.p2.x) - x0;
            const double nn = nx * nx + ny * ny;
            if (nn > 1e-9) {
                const double t = ((x1 - x0) * nx + (y1 - y0) * ny) / nn;
                x1 = x0 + nx * t;
                y1 = y0 + ny * t;
            }
            out.p0 = to_em(m, x0, y0);
            out.p1 = to_em(m, x1, y1);
            return !out.stops.empty();
        }
        case FT_COLR_PAINTFORMAT_RADIAL_GRADIENT: {
            // Two-circle gradients are approximated by concentric ones around the end circle.
            const FT_PaintRadialGradient& g = paint.u.radial_gradient;
            out.kind = ColorPaint::Kind::radial;
            out.stops = stops_of(g.colorline);
            out.p0 = to_em(m, fixed(g.c1.x), fixed(g.c1.y));
            const double k = m.scale() * em_;
            out.r0 = static_cast<float>(fixed(g.r0) * k);
            out.r1 = static_cast<float>(fixed(g.r1) * k);
            return !out.stops.empty();
        }
        case FT_COLR_PAINTFORMAT_SWEEP_GRADIENT: {
            // Sweep gradients aren't supported yet: their average color.
            std::vector<ColorStop> stops = stops_of(paint.u.sweep_gradient.colorline);
            if (stops.empty()) {
                return false;
            }
            out.kind = ColorPaint::Kind::solid;
            Color sum{0, 0, 0, 0};
            for (const ColorStop& s : stops) {
                sum = {sum.r + s.color.r, sum.g + s.color.g, sum.b + s.color.b, sum.a + s.color.a};
            }
            const float n = static_cast<float>(stops.size());
            out.color = {sum.r / n, sum.g / n, sum.b / n, sum.a / n};
            out.foreground = stops.front().foreground;
            return true;
        }
        default: {
            Affine t;
            FT_OpaquePaint child{};
            return transform_of(paint, t, child) && resolve_fill(child, m * t, out, depth + 1);
        }
        }
    }

    FT_Face face_;
    std::span<const Color> palette_;
    double em_;
    std::vector<ColorPath>& out_;
};

} // namespace

std::span<const ColorPath> FontFace::color_paths(std::uint32_t glyph) const {
    if (palette_.empty()) {
        return {};
    }
    auto it = color_paths_.find(glyph);
    if (it == color_paths_.end()) {
        std::vector<ColorPath> paths;
        FT_OpaquePaint root{nullptr, 1};
        if (FT_Get_Color_Glyph_Paint(ft_face_, glyph, FT_COLOR_NO_ROOT_TRANSFORM, &root)) {
            PaintWalker(ft_face_, palette_, units_per_em_, paths).walk(root, {}, 0);
        }
        it = color_paths_.emplace(glyph, std::move(paths)).first;
    }
    return it->second;
}

} // namespace elg::detail
