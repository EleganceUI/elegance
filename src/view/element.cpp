// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include "view/element.h"

#include "view/focus.h"
#include "view/view_node.h"

#include <algorithm>
#include <cmath>

namespace elg::detail {

namespace {

// Distinct proposals per element per frame are few (stacks probe min, max, and the final size).
constexpr std::size_t kSizeCacheLimit = 16;

// What shapes, canvases, and other "fill the proposal" views report for unspecified proposals.
constexpr Size kDefaultIdealSize{10, 10};

bool drawable(const Rect& r) {
    return std::isfinite(r.x) && std::isfinite(r.y) && std::isfinite(r.width) &&
           std::isfinite(r.height) && r.width > 0 && r.height > 0;
}

} // namespace

// --- Element ---

Element::~Element() {
    if (focus_manager_) {
        focus_manager_->forget(this); // also ends a .focused watch on this element
    }
}

void Element::paint_focus_ring(DisplayList& list) const {
    if (!focus_ring_color_ || focus_manager_->focused() != this || !focus_manager_->ring_visible()) {
        return;
    }
    constexpr float kGap = 3;
    constexpr float kWidth = 2;
    constexpr float kRadius = 8;
    list.commands.push_back(RectCommand{frame().inset(-kGap - kWidth), CornerRadii::all(kRadius + kGap),
                                        ShapeStyle(Color::clear), focus_ring_color_(), kWidth});
}

Size Element::size_that_fits(ProposedSize proposal) {
    for (const auto& [cached_proposal, size] : size_cache_) {
        if (cached_proposal == proposal) {
            return size;
        }
    }
    const Size size = measure(proposal);
    if (size_cache_.size() >= kSizeCacheLimit) {
        size_cache_.clear();
    }
    size_cache_.emplace_back(proposal, size);
    return size;
}

void Element::invalidate_layout() noexcept {
    for (Element* element = this; element; element = element->parent_) {
        element->size_cache_.clear();
        element->on_layout_invalidated();
    }
}

const Animation* Element::change_animation() const noexcept {
    if (const Animation* animation = current_animation()) {
        return animation;
    }
    return implicit_ && implicit_->fired() ? &implicit_->animation : nullptr;
}

void Element::place(Point position, ProposedSize proposal) {
    const Size size = size_that_fits(proposal);
    const Rect target{position.x, position.y, size.width, size.height};
    if (!placed_ || target != frame_.target()) {
        // An element appearing for the first time takes its frame at once; afterwards, a layout
        // change caused by an animated update eases there.
        const Animation* animation = nullptr;
        if (placed_ && animates_layout_ && !snap_next_layout_) {
            animation = layout_animation();
            if (!animation && implicit_ && implicit_->fired()) {
                animation = &implicit_->animation;
            }
        }
        frame_.set(target, animation);
        placed_ = true;
    }
    snap_next_layout_ = false;
    last_proposal_ = proposal;
    arrange(target, proposal);
}

// --- ShapeElement ---

Size ShapeElement::measure(ProposedSize proposal) {
    return proposal.replacing_unspecified(kDefaultIdealSize);
}

ShapeOutline shape_outline(const ShapeSpec& spec, Rect frame) {
    switch (spec.kind) {
    case ShapeKind::path: return {frame, {}};
    case ShapeKind::rectangle: return {frame, {}};
    case ShapeKind::rounded_rectangle: return {frame, spec.radii};
    case ShapeKind::capsule: return {frame, CornerRadii::all(std::min(frame.width, frame.height) / 2)};
    case ShapeKind::circle: {
        const float d = std::min(frame.width, frame.height);
        return {{frame.x + (frame.width - d) / 2, frame.y + (frame.height - d) / 2, d, d}, CornerRadii::all(d / 2)};
    }
    }
    return {frame, {}};
}

namespace {

// A rounded rect's outline as a path (per-corner radii), for dashed strokes of the basic shapes.
Path rounded_rect_path(Rect r, const CornerRadii& radii) {
    constexpr float kKappa = 0.5522847498f;
    const float max_radius = std::min(r.width, r.height) / 2;
    const auto clamp = [&](float v) { return std::clamp(v, 0.f, max_radius); };
    const float tl = clamp(radii.top_left), tr = clamp(radii.top_right), br = clamp(radii.bottom_right),
                bl = clamp(radii.bottom_left);
    Path path;
    path.move_to({r.x + tl, r.y}).line_to({r.max_x() - tr, r.y});
    if (tr > 0) {
        path.cubic_to({r.max_x() - tr * (1 - kKappa), r.y}, {r.max_x(), r.y + tr * (1 - kKappa)}, {r.max_x(), r.y + tr});
    }
    path.line_to({r.max_x(), r.max_y() - br});
    if (br > 0) {
        path.cubic_to({r.max_x(), r.max_y() - br * (1 - kKappa)}, {r.max_x() - br * (1 - kKappa), r.max_y()},
                      {r.max_x() - br, r.max_y()});
    }
    path.line_to({r.x + bl, r.max_y()});
    if (bl > 0) {
        path.cubic_to({r.x + bl * (1 - kKappa), r.max_y()}, {r.x, r.max_y() - bl * (1 - kKappa)}, {r.x, r.max_y() - bl});
    }
    path.line_to({r.x, r.y + tl});
    if (tl > 0) {
        path.cubic_to({r.x, r.y + tl * (1 - kKappa)}, {r.x + tl * (1 - kKappa), r.y}, {r.x + tl, r.y});
    }
    return path.close();
}

std::shared_ptr<const std::vector<std::vector<Point>>> polygons_of(const Path& path, float tolerance, Rect& bounds) {
    auto polygons = std::make_shared<std::vector<std::vector<Point>>>();
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    bool any = false;
    for (Path::Polyline& line : path.flatten(tolerance)) {
        if (line.points.size() < 3) {
            continue; // filling closes every subpath; fewer than 3 points enclose nothing
        }
        for (const Point p : line.points) {
            if (!any) {
                x0 = x1 = p.x;
                y0 = y1 = p.y;
                any = true;
            }
            x0 = std::min(x0, p.x);
            y0 = std::min(y0, p.y);
            x1 = std::max(x1, p.x);
            y1 = std::max(y1, p.y);
        }
        polygons->push_back(std::move(line.points));
    }
    bounds = {x0, y0, x1 - x0, y1 - y0};
    return polygons;
}

bool polygons_contain(const std::vector<std::vector<Point>>& polygons, Point p, FillRule rule) {
    int winding = 0;
    for (const std::vector<Point>& polygon : polygons) {
        for (std::size_t i = 0; i < polygon.size(); ++i) {
            const Point a = polygon[i];
            const Point b = polygon[(i + 1) % polygon.size()];
            const float side = (b.x - a.x) * (p.y - a.y) - (p.x - a.x) * (b.y - a.y);
            if (a.y <= p.y) {
                if (b.y > p.y && side > 0) {
                    ++winding;
                }
            } else if (b.y <= p.y && side < 0) {
                --winding;
            }
        }
    }
    return rule == FillRule::non_zero ? winding != 0 : (winding % 2) != 0;
}

} // namespace

// The shape's outline as a path, for a frame `local` at the origin.
Path shape_path(const ShapeSpec& spec, Rect local) {
    if (spec.kind == ShapeKind::path && spec.path) {
        const ShapePath& source = *spec.path;
        if (source.builder) {
            return source.builder(local);
        }
        if (source.view_box && source.view_box->width > 0 && source.view_box->height > 0) {
            // Fit the view box in the frame, keeping its aspect ratio, centered (SVG's "meet").
            const Rect box = *source.view_box;
            const float k = std::min(local.width / box.width, local.height / box.height);
            const float dx = (local.width - box.width * k) / 2 - box.x * k;
            const float dy = (local.height - box.height * k) / 2 - box.y * k;
            return source.path.transformed(k, k, dx, dy);
        }
        return source.path;
    }
    const ShapeOutline o = shape_outline(spec, local);
    return rounded_rect_path(o.rect, o.radii);
}

// Flattened to a quarter pixel.
float flatten_tolerance(float scale) {
    return 0.25f / std::max(scale, 0.25f);
}

ShapeFill shape_fill(const ShapeSpec& spec, Size size, float scale) {
    ShapeFill fill;
    fill.polygons = polygons_of(shape_path(spec, {0, 0, size.width, size.height}), flatten_tolerance(scale),
                                fill.bounds);
    fill.rule = spec.kind == ShapeKind::path && spec.path ? spec.path->rule : FillRule::non_zero;
    return fill;
}

bool shape_fill_contains(const ShapeFill& fill, Point local) {
    return fill.polygons && polygons_contain(*fill.polygons, local, fill.rule);
}

bool ShapeElement::uses_path() const noexcept {
    return spec_.kind == ShapeKind::path || (spec_.stroke && !spec_.stroke->style.dash.empty());
}

const ShapeElement::Geometry& ShapeElement::geometry(Size size, float scale) const {
    if (geometry_.size.width == size.width && geometry_.size.height == size.height && geometry_.scale == scale) {
        return geometry_;
    }
    const float tolerance = flatten_tolerance(scale);
    const Rect local{0, 0, size.width, size.height};
    const Path outline = shape_path(spec_, local);

    Geometry g;
    g.size = size;
    g.scale = scale;
    if (spec_.kind == ShapeKind::path) {
        g.fill = polygons_of(outline, tolerance, g.fill_bounds);
    }
    if (spec_.stroke) {
        const ShapeStroke& stroke = *spec_.stroke;
        Path line = outline;
        if (!stroke.centered && spec_.kind != ShapeKind::path) {
            // Inside the edge: stroke the outline inset by half the width.
            const ShapeOutline o = shape_outline(spec_, local);
            const float half = stroke.width / 2;
            const auto shrink = [&](float r) { return r > 0 ? std::max(0.f, r - half) : 0.f; };
            line = rounded_rect_path(o.rect.inset(half), {shrink(o.radii.top_left), shrink(o.radii.top_right),
                                                          shrink(o.radii.bottom_right), shrink(o.radii.bottom_left)});
        }
        g.stroke = polygons_of(line.stroked(stroke.style, tolerance), tolerance, g.stroke_bounds);
    }
    geometry_ = std::move(g);
    return geometry_;
}

void ShapeElement::paint_impl(DisplayList& list) const {
    if (!drawable(frame())) {
        return;
    }
    if (uses_path()) {
        if (fill_.animating() || stroke_color_.animating()) {
            list.animating = true;
        }
        const Rect bounds = frame();
        const Geometry& g = geometry(bounds.size(), list.scale > 0 ? list.scale : 1.f);
        const Point origin{bounds.x, bounds.y};
        const ShapeStyle fill = has_fill_ ? fill_.current() : ShapeStyle(Color::clear);
        if (spec_.kind == ShapeKind::path) {
            if (g.fill && !is_clear(fill)) {
                list.commands.push_back(PathCommand{g.fill, g.fill_bounds, 1, origin, bounds, fill, spec_.path->rule});
            }
        } else if (!is_clear(fill)) {
            const auto [rect, radii] = shape_outline(spec_, bounds);
            list.commands.push_back(RectCommand{rect, radii, fill, Color::clear, 0});
        }
        if (g.stroke) {
            list.commands.push_back(PathCommand{g.stroke, g.stroke_bounds, 1, origin, bounds,
                                                ShapeStyle(stroke_color_.current()), FillRule::non_zero});
        }
        return;
    }
    const auto [rect, radii] = shape_outline(spec_, frame());

    // A stroked shape isn't filled unless asked to be (as in SwiftUI).
    if (fill_.animating() || stroke_color_.animating()) {
        list.animating = true;
    }
    const ShapeStyle fill = has_fill_ ? fill_.current() : ShapeStyle(Color::clear);
    const Color stroke_color = stroke_color_.current();
    if (!spec_.stroke) {
        list.commands.push_back(RectCommand{rect, radii, fill, Color::clear, 0});
        return;
    }

    const ShapeStroke& stroke = *spec_.stroke;
    if (!stroke.centered) {
        list.commands.push_back(RectCommand{rect, radii, fill, stroke_color, stroke.width});
        return;
    }

    // Centered: the line straddles the edge, so it's a border on a rect grown by half the width.
    if (!is_clear(fill)) {
        list.commands.push_back(RectCommand{rect, radii, fill, Color::clear, 0});
    }
    const float half = stroke.width / 2;
    const auto grow = [&](float r) { return r > 0 ? r + half : 0.f; };
    list.commands.push_back(RectCommand{rect.inset(-half),
                                        {grow(radii.top_left), grow(radii.top_right),
                                         grow(radii.bottom_right), grow(radii.bottom_left)},
                                        Color::clear, stroke_color, stroke.width});
}

bool ShapeElement::hit_path(Point point) const {
    const Rect bounds = frame();
    const Point local{point.x - bounds.x, point.y - bounds.y};
    const Geometry& g = geometry(bounds.size(), geometry_.scale > 0 ? geometry_.scale : 1.f);
    if (spec_.kind == ShapeKind::path && spec_.fill && g.fill &&
        polygons_contain(*g.fill, local, spec_.path ? spec_.path->rule : FillRule::non_zero)) {
        return true;
    }
    if (g.stroke && polygons_contain(*g.stroke, local, FillRule::non_zero)) {
        return true;
    }
    // An unstyled path (environment foreground) is filled.
    return spec_.kind == ShapeKind::path && !spec_.stroke && g.fill &&
           polygons_contain(*g.fill, local, spec_.path ? spec_.path->rule : FillRule::non_zero);
}

// --- SpacerElement ---

Size SpacerElement::measure(ProposedSize proposal) {
    const auto along = [&](std::optional<float> proposed) {
        return std::max(min_length_, proposed.value_or(min_length_));
    };
    if (axis_ == Axis::horizontal) {
        return {along(proposal.width), 0};
    }
    if (axis_ == Axis::vertical) {
        return {0, along(proposal.height)};
    }
    return {along(proposal.width), along(proposal.height)};
}

// --- CanvasElement ---

Size CanvasElement::measure(ProposedSize proposal) {
    return proposal.replacing_unspecified(kDefaultIdealSize);
}

void CanvasElement::paint_impl(DisplayList& list) const {
    if (!draw_ || !drawable(frame())) {
        return;
    }
    GraphicsContext context(list, frame(), list.scale);
    draw_(context);
}

// --- LayoutElement ---

LayoutElement::LayoutElement(std::shared_ptr<const Layout> layout,
                             std::vector<std::unique_ptr<Element>> children)
    : layout_(std::move(layout)), children_(std::move(children)) {
    for (const auto& child : children_) {
        adopt(*child);
    }
    rebuild_children();
}

void LayoutElement::rebuild_children() {
    laid_out_.clear();
    for (const auto& child : children_) {
        child->collect_layout_children(laid_out_);
    }
    handles_.clear();
    handles_.reserve(laid_out_.size());
    for (Element* child : laid_out_) {
        handles_.emplace_back(*child);
    }
}

const void* LayoutModifierElement::find_layout_value(const std::type_info& key) const noexcept {
    if (const auto* carrier = dynamic_cast<const LayoutValueModifierBase*>(modifier_.get())) {
        if (const void* value = carrier->value_for(key)) {
            return value;
        }
    }
    return content_->find_layout_value(key);
}

void LayoutElement::on_structure_changed() {
    // An If/ForEach inside changed what it shows. This layout is still one element to its
    // parent, so the change stops here.
    rebuild_children();
    invalidate_layout();
}

Size LayoutElement::measure(ProposedSize proposal) {
    return layout_->cached_size_that_fits(proposal, handles_, cache_);
}

void LayoutElement::arrange(Rect bounds, ProposedSize proposal) {
    for (const auto& child : children_) {
        child->prune_exiting();
    }
    layout_->cached_place_children(bounds, proposal, handles_, cache_);
}

// Back to front: by z-index, then (exiting children after the rest, then) declaration order.
std::vector<Element*> LayoutElement::paint_order() const {
    std::vector<Element*> order = laid_out_;
    // Removed children still animating out, where they last were.
    for (const auto& child : children_) {
        child->collect_exiting(order);
    }
    std::ranges::stable_sort(order, {}, [](const Element* element) { return element->z_index(); });
    return order;
}

void LayoutElement::paint_impl(DisplayList& list) const {
    for (const Element* child : paint_order()) {
        child->paint(list);
    }
}

// --- TransitionElement ---

void TransitionElement::insert(const Animation* animation) {
    if (animation) {
        progress_.set(0.f, nullptr);
        progress_.set(1.f, animation);
    }
}

bool TransitionElement::remove(const Animation* animation) {
    if (!animation) {
        return false;
    }
    removing_ = true;
    progress_.set(0.f, animation);
    return true;
}

void TransitionElement::paint_impl(DisplayList& list) const {
    const float progress = progress_.current();
    if (progress_.animating()) {
        list.animating = true;
    }
    if (progress >= 1 && !removing_) {
        content_->paint(list);
        return;
    }
    const Rect bounds = frame();
    const Transition::Pose pose = transition_.pose(progress, removing_, bounds.size());
    DisplayList content;
    content.scale = list.scale;
    content_->paint(content);
    list.animating |= content.animating;
    transform(content.commands, pose.x, pose.y, pose.scale, bounds.center());
    append_faded(list, std::move(content), pose.opacity);
}

// --- LayoutModifierElement ---

float LayoutModifierElement::layout_priority() const noexcept {
    return modifier_->layout_priority().value_or(content_->layout_priority());
}

float LayoutModifierElement::z_index() const noexcept {
    return content_->z_index();
}

Size LayoutModifierElement::measure(ProposedSize proposal) {
    return modifier_->size_that_fits(proposal, LayoutChild(*content_));
}

void LayoutModifierElement::arrange(Rect bounds, ProposedSize proposal) {
    modifier_->place(bounds, proposal, LayoutChild(*content_));
}

// --- EffectElement ---

const ShapeFill* EffectElement::clip_path(float scale) const {
    if (!effects_.clip_shape) {
        return nullptr;
    }
    const auto& node = static_cast<const ShapeNode&>(*ViewAccess::node(*effects_.clip_shape));
    if (node.spec().kind != ShapeKind::path) {
        return nullptr;
    }
    const Size size = frame().size();
    if (size.width != clip_size_.width || size.height != clip_size_.height || scale != clip_scale_) {
        clip_fill_ = shape_fill(node.spec(), size, scale);
        clip_size_ = size;
        clip_scale_ = scale;
    }
    return &clip_fill_;
}

void EffectElement::paint_impl(DisplayList& list) const {
    // Paint the content (clipped) on its own first, to see what it consists of.
    DisplayList content;
    content.scale = list.scale;
    const ShapeFill* mask = clip_path(list.scale > 0 ? list.scale : 1.f);
    if (mask) {
        // A path clip: the content renders offscreen and only what the path covers is kept.
        // A rect clip to the same bounds first lets everything outside be culled.
        const Rect bounds = frame();
        const Rect mask_bounds{bounds.x + mask->bounds.x, bounds.y + mask->bounds.y, mask->bounds.width,
                               mask->bounds.height};
        content.commands.push_back(PushLayerCommand{
            mask_bounds, 1, std::nullopt,
            PathCommand{mask->polygons, mask->bounds, 1, {bounds.x, bounds.y}, bounds, ShapeStyle(Color::white),
                        mask->rule}});
        content.commands.push_back(PushClipCommand{mask_bounds, {}});
        content_->paint(content);
        content.commands.push_back(PopClipCommand{});
        content.commands.push_back(PopLayerCommand{});
        list.animating |= content.animating;
    }
    const bool clips = !mask && (effects_.clip_to_bounds || effects_.clip_shape);
    if (clips) {
        ShapeOutline clip{frame(), {}};
        if (effects_.clip_shape) {
            const auto& node = static_cast<const ShapeNode&>(*ViewAccess::node(*effects_.clip_shape));
            clip = shape_outline(node.spec(), frame());
        }
        content.commands.push_back(PushClipCommand{clip.rect, clip.radii});
    }
    if (!mask) {
        content_->paint(content);
        list.animating |= content.animating;
    }
    if (clips) {
        content.commands.push_back(PopClipCommand{});
    }
    if (offset_x_.animating() || offset_y_.animating()) {
        list.animating = true;
    }
    transform(content.commands, offset_x_.current(), offset_y_.current(), 1, {});

    if (opacity_.animating()) {
        list.animating = true;
    }
    const float opacity = std::clamp(opacity_.current(), 0.f, 1.f);
    const std::optional<Shadow>& shadow = effects_.shadow;
    const auto append_content = [&] {
        list.commands.insert(list.commands.end(), std::make_move_iterator(content.commands.begin()),
                             std::make_move_iterator(content.commands.end()));
    };

    if (opacity >= 1 && !shadow) {
        append_content();
        return;
    }
    if (opacity <= 0 || content.commands.empty()) {
        return;
    }

    // Fast path: a single filled shape. Fading it is an alpha multiply, and its shadow is an
    // analytic blurred rounded rect; no offscreen layer needed.
    if (content.commands.size() == 1) {
        if (auto* rect = std::get_if<RectCommand>(&content.commands.front()); rect && !is_clear(rect->fill)) {
            if (shadow) {
                const Rect offset{rect->rect.x + shadow->x, rect->rect.y + shadow->y, rect->rect.width,
                                  rect->rect.height};
                list.commands.push_back(
                    BoxShadowCommand{offset, rect->radii, shadow->color.opacity(opacity), shadow->radius});
            }
            rect->fill = with_opacity(rect->fill, opacity);
            rect->border = rect->border.opacity(opacity);
            append_content();
            return;
        }
    }

    // General path: render offscreen, then composite with opacity and a blurred shadow.
    std::optional<Rect> bounds;
    for (const DrawCommand& command : content.commands) {
        if (const std::optional<Rect> b = draw_bounds(command)) {
            bounds = bounds ? union_of(*bounds, *b) : *b;
        }
    }
    if (!bounds) {
        return;
    }
    list.commands.push_back(PushLayerCommand{*bounds, opacity, shadow});
    append_content();
    list.commands.push_back(PopLayerCommand{});
}

// --- DecorationElement ---

void DecorationElement::arrange(Rect bounds, ProposedSize proposal) {
    content_->place({bounds.x, bounds.y}, proposal);

    // The decoration is offered exactly the content's size and aligned within it.
    const ProposedSize offer{bounds.width, bounds.height};
    const Size size = decoration_->size_that_fits(offer);
    decoration_->place(aligned_position(bounds, size, modifier_->alignment()), offer);
}

void DecorationElement::paint_impl(DisplayList& list) const {
    if (modifier_->placement() == DecorationModifier::Placement::behind) {
        decoration_->paint(list);
        content_->paint(list);
    } else {
        content_->paint(list);
        decoration_->paint(list);
    }
}

} // namespace elg::detail
