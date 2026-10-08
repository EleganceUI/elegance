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
        focus_manager_->forget(this);
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
    arrange(target, proposal);
}

// --- ShapeElement ---

Size ShapeElement::measure(ProposedSize proposal) {
    return proposal.replacing_unspecified(kDefaultIdealSize);
}

ShapeOutline shape_outline(const ShapeSpec& spec, Rect frame) {
    switch (spec.kind) {
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

void ShapeElement::paint_impl(DisplayList& list) const {
    if (!drawable(frame())) {
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

void LayoutElement::on_structure_changed() {
    // An If/ForEach inside changed what it shows. This layout is still one element to its
    // parent, so the change stops here.
    rebuild_children();
    invalidate_layout();
}

Size LayoutElement::measure(ProposedSize proposal) {
    return layout_->size_that_fits(proposal, handles_);
}

void LayoutElement::arrange(Rect bounds, ProposedSize proposal) {
    for (const auto& child : children_) {
        child->prune_exiting();
    }
    layout_->place_children(bounds, proposal, handles_);
}

void LayoutElement::paint_impl(DisplayList& list) const {
    for (const Element* child : laid_out_) {
        child->paint(list);
    }
    // Removed children still animating out, where they last were.
    std::vector<Element*> exiting;
    for (const auto& child : children_) {
        child->collect_exiting(exiting);
    }
    for (const Element* child : exiting) {
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

Size LayoutModifierElement::measure(ProposedSize proposal) {
    return modifier_->size_that_fits(proposal, LayoutChild(*content_));
}

void LayoutModifierElement::arrange(Rect bounds, ProposedSize proposal) {
    modifier_->place(bounds, proposal, LayoutChild(*content_));
}

// --- EffectElement ---

void EffectElement::paint_impl(DisplayList& list) const {
    // Paint the content (clipped) on its own first, to see what it consists of.
    DisplayList content;
    content.scale = list.scale;
    const bool clips = effects_.clip_to_bounds || effects_.clip_shape;
    if (clips) {
        ShapeOutline clip{frame(), {}};
        if (effects_.clip_shape) {
            const auto& node = static_cast<const ShapeNode&>(*ViewAccess::node(*effects_.clip_shape));
            clip = shape_outline(node.spec(), frame());
        }
        content.commands.push_back(PushClipCommand{clip.rect, clip.radii});
    }
    content_->paint(content);
    list.animating |= content.animating;
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
