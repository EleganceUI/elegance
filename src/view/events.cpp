// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Hit testing, the built-in event modifiers, and the Button element (design.md §10).

#include "view/element.h"
#include "view/view_node.h"

#include <elg/modifiers/events.h>

#include <algorithm>

namespace elg {

// --- EventHandler defaults ---

EventHandler::~EventHandler() = default;

EventResult EventHandler::pointer(const PointerEvent&, Rect) {
    return {};
}

EventResult EventHandler::hover(bool) {
    return {};
}

namespace {

bool contains(const Rect& rect, Point point) {
    return point.x >= rect.x && point.y >= rect.y && point.x < rect.max_x() && point.y < rect.max_y();
}

class TapHandler final : public EventHandler {
public:
    TapHandler(int count, std::function<void()> action) : count_(count), action_(std::move(action)) {}

    EventResult pointer(const PointerEvent& event, Rect frame) override {
        if (event.button != MouseButton::left) {
            return {};
        }
        switch (event.kind) {
        case PointerEvent::Kind::down:
            // A double-tap handler leaves first clicks to outer (single-tap) handlers.
            if (event.click_count < count_) {
                return {};
            }
            pressed_ = true;
            return {.consumed = true};
        case PointerEvent::Kind::up: {
            const bool fire = pressed_ && contains(frame, event.location) && event.click_count >= count_ &&
                              (event.click_count - count_) % std::max(count_, 1) == 0;
            pressed_ = false;
            if (fire && action_) {
                action_();
            }
            return {.consumed = true};
        }
        case PointerEvent::Kind::cancel: pressed_ = false; return {};
        case PointerEvent::Kind::move: return {};
        }
        return {};
    }

private:
    int count_;
    std::function<void()> action_;
    bool pressed_ = false;
};

class HoverHandler final : public EventHandler {
public:
    explicit HoverHandler(std::function<void(bool)> action) : action_(std::move(action)) {}

    EventResult hover(bool inside) override {
        if (action_) {
            action_(inside);
        }
        return {};
    }

private:
    std::function<void(bool)> action_;
};

} // namespace

std::unique_ptr<EventHandler> TapGestureModifier::make_handler() const {
    return std::make_unique<TapHandler>(count_, action_);
}

std::unique_ptr<EventHandler> HoverModifier::make_handler() const {
    return std::make_unique<HoverHandler>(action_);
}

} // namespace elg

namespace elg::detail {

namespace {

bool contains(const Rect& rect, Point point) {
    return point.x >= rect.x && point.y >= rect.y && point.x < rect.max_x() && point.y < rect.max_y();
}

// Whether `point` is inside a rounded rect.
bool inside(Point point, const ShapeOutline& outline) {
    const Rect& r = outline.rect;
    if (!contains(r, point)) {
        return false;
    }
    const float max_radius = 0.5f * std::min(r.width, r.height);
    const auto in_corner = [&](float radius, float cx, float cy) {
        radius = std::clamp(radius, 0.f, max_radius);
        const float x = cx < r.center().x ? r.x + radius : r.max_x() - radius;
        const float y = cy < r.center().y ? r.y + radius : r.max_y() - radius;
        const bool beyond_x = cx < r.center().x ? point.x < x : point.x > x;
        const bool beyond_y = cy < r.center().y ? point.y < y : point.y > y;
        if (!beyond_x || !beyond_y) {
            return true; // not in this corner's square
        }
        const float dx = point.x - x;
        const float dy = point.y - y;
        return dx * dx + dy * dy <= radius * radius;
    };
    return in_corner(outline.radii.top_left, r.x, r.y) && in_corner(outline.radii.top_right, r.max_x(), r.y) &&
           in_corner(outline.radii.bottom_right, r.max_x(), r.max_y()) &&
           in_corner(outline.radii.bottom_left, r.x, r.max_y());
}

ShapeOutline outline_of(const Shape& shape, Rect frame) {
    return shape_outline(static_cast<const ShapeNode&>(*ViewAccess::node(shape)).spec(), frame);
}

Color mix(Color a, Color b, float t) {
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
}

constexpr float kButtonPaddingX = 14;
constexpr float kButtonPaddingY = 7;
constexpr float kButtonRadius = 8;

} // namespace

// --- Hit testing ---

bool Element::hit_test(Point point, std::vector<Element*>& path) {
    path.push_back(this);
    if (hit_impl(point, path)) {
        return true;
    }
    path.pop_back();
    return false;
}

bool ShapeElement::hit_impl(Point point, std::vector<Element*>&) {
    const ShapeOutline outline = shape_outline(spec_, frame());
    if (!spec_.stroke || spec_.fill) {
        return inside(point, outline);
    }
    // A stroke-only shape (e.g. a border overlay) is hit only on its line, as in SwiftUI, so it
    // doesn't block what it outlines.
    const ShapeStroke& stroke = *spec_.stroke;
    const float outer_grow = stroke.centered ? stroke.width / 2 : 0.f;
    const float inner_shrink = stroke.centered ? stroke.width / 2 : stroke.width;
    const auto adjust = [](const CornerRadii& radii, float delta) {
        const auto r = [&](float value) { return value > 0 ? std::max(0.f, value + delta) : 0.f; };
        return CornerRadii{r(radii.top_left), r(radii.top_right), r(radii.bottom_right), r(radii.bottom_left)};
    };
    const ShapeOutline outer{outline.rect.inset(-outer_grow), adjust(outline.radii, outer_grow)};
    const ShapeOutline inner{outline.rect.inset(inner_shrink), adjust(outline.radii, -inner_shrink)};
    return inside(point, outer) && !inside(point, inner);
}

bool CanvasElement::hit_impl(Point point, std::vector<Element*>&) {
    return contains(frame(), point);
}

bool TextElement::hit_impl(Point point, std::vector<Element*>&) {
    return contains(frame(), point);
}

bool LayoutElement::hit_impl(Point point, std::vector<Element*>& path) {
    // Front to back: later children are drawn on top.
    for (auto it = laid_out_.rbegin(); it != laid_out_.rend(); ++it) {
        if ((*it)->hit_test(point, path)) {
            return true;
        }
    }
    return false;
}

bool EffectElement::hit_impl(Point point, std::vector<Element*>& path) {
    if (!effects_.allows_hit_testing) {
        return false;
    }
    if (effects_.content_shape) {
        if (!inside(point, outline_of(*effects_.content_shape, frame()))) {
            return false;
        }
        content_->hit_test(point, path); // record what's under the pointer, if anything
        return true;
    }
    if (effects_.clip_shape && !inside(point, outline_of(*effects_.clip_shape, frame()))) {
        return false;
    }
    if (effects_.clip_to_bounds && !contains(frame(), point)) {
        return false;
    }
    return content_->hit_test(point, path);
}

bool DecorationElement::hit_impl(Point point, std::vector<Element*>& path) {
    if (modifier_->placement() == DecorationModifier::Placement::in_front) {
        return decoration_->hit_test(point, path) || content_->hit_test(point, path);
    }
    return content_->hit_test(point, path) || decoration_->hit_test(point, path);
}

// --- ButtonElement ---

Size ButtonElement::measure(ProposedSize proposal) {
    const auto shrink = [](std::optional<float> length, float amount) -> std::optional<float> {
        return length ? std::optional<float>(std::max(0.f, *length - amount)) : std::nullopt;
    };
    const Size label = label_->size_that_fits(
        {shrink(proposal.width, 2 * kButtonPaddingX), shrink(proposal.height, 2 * kButtonPaddingY)});
    return {label.width + 2 * kButtonPaddingX, label.height + 2 * kButtonPaddingY};
}

void ButtonElement::arrange(Rect bounds, ProposedSize) {
    label_->place({bounds.x + kButtonPaddingX, bounds.y + kButtonPaddingY},
                  {bounds.width - 2 * kButtonPaddingX, bounds.height - 2 * kButtonPaddingY});
}

void ButtonElement::paint_impl(DisplayList& list) const {
    Color fill = tint_;
    if (pressed_ && hovered_) {
        fill = mix(tint_, Color::black, 0.2f);
    } else if (hovered_) {
        fill = mix(tint_, Color::white, 0.15f);
    }
    list.commands.push_back(RectCommand{frame(), CornerRadii::all(kButtonRadius), fill, Color::clear, 0});
    label_->paint(list);
}

bool ButtonElement::hit_impl(Point point, std::vector<Element*>&) {
    return inside(point, {frame(), CornerRadii::all(kButtonRadius)});
}

EventResult ButtonElement::pointer(const PointerEvent& event, Rect frame) {
    if (event.button != MouseButton::left && event.kind != PointerEvent::Kind::cancel) {
        return {};
    }
    switch (event.kind) {
    case PointerEvent::Kind::down:
        pressed_ = true;
        return {.consumed = true, .redraw = true};
    case PointerEvent::Kind::move: {
        // Looks pressed only while the pointer is over it, like native buttons.
        const bool over = contains(frame, event.location);
        const bool redraw = over != hovered_;
        hovered_ = over;
        return {.consumed = true, .redraw = redraw};
    }
    case PointerEvent::Kind::up: {
        const bool fire = pressed_ && contains(frame, event.location);
        pressed_ = false;
        if (fire && action_) {
            action_();
        }
        return {.consumed = true, .redraw = true};
    }
    case PointerEvent::Kind::cancel:
        pressed_ = false;
        hovered_ = false;
        return {.redraw = true};
    }
    return {};
}

EventResult ButtonElement::hover(bool inside) {
    hovered_ = inside;
    return {.redraw = true};
}

} // namespace elg::detail
