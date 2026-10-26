// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Hit testing, the built-in event modifiers, and the Button element (design.md §10).

#include "view/element.h"
#include "view/window_drag.h"
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

EventResult EventHandler::scroll(const ScrollEvent&) {
    return {};
}

PointerStyle EventHandler::pointer_style(Point) const {
    return PointerStyle::automatic;
}

EventResult EventHandler::magnify(const MagnifyEvent&) {
    return {};
}

EventResult EventHandler::rotate(const RotateEvent&) {
    return {};
}

EventResult EventHandler::smart_magnify(Point) {
    return {};
}

EventResult EventHandler::key(const KeyEvent&) {
    return {};
}

EventResult EventHandler::focus(bool) {
    return {};
}

EventResult EventHandler::text_input(const TextInputEvent&) {
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
        case PointerEvent::Kind::move:
        case PointerEvent::Kind::hover: return {};
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
    HoverHandler(std::function<void(bool)> action, std::optional<HoverDelay> delay)
        : action_(std::move(action)), delay_(delay) {}

    EventResult hover(bool inside) override {
        if (action_) {
            action_(inside);
        }
        return {};
    }

    std::optional<HoverDelay> hover_delay() const override { return delay_; }

private:
    std::function<void(bool)> action_;
    std::optional<HoverDelay> delay_;
};

class KeyPressHandler final : public EventHandler {
public:
    explicit KeyPressHandler(std::function<bool(const KeyEvent&)> action) : action_(std::move(action)) {}

    EventResult key(const KeyEvent& event) override {
        if (event.kind == KeyEvent::Kind::down && action_ && action_(event)) {
            return {.consumed = true};
        }
        return {};
    }

private:
    std::function<bool(const KeyEvent&)> action_;
};

class MagnifyHandler final : public EventHandler {
public:
    explicit MagnifyHandler(std::function<void(const MagnifyEvent&)> action) : action_(std::move(action)) {}

    EventResult magnify(const MagnifyEvent& event) override {
        if (!action_) {
            return {};
        }
        action_(event);
        return {.consumed = true};
    }

private:
    std::function<void(const MagnifyEvent&)> action_;
};

class RotateHandler final : public EventHandler {
public:
    explicit RotateHandler(std::function<void(const RotateEvent&)> action) : action_(std::move(action)) {}

    EventResult rotate(const RotateEvent& event) override {
        if (!action_) {
            return {};
        }
        action_(event);
        return {.consumed = true};
    }

private:
    std::function<void(const RotateEvent&)> action_;
};

class SmartMagnifyHandler final : public EventHandler {
public:
    explicit SmartMagnifyHandler(std::function<void(Point)> action) : action_(std::move(action)) {}

    EventResult smart_magnify(Point location) override {
        if (!action_) {
            return {};
        }
        action_(location);
        return {.consumed = true};
    }

private:
    std::function<void(Point)> action_;
};

class PointerStyleHandler final : public EventHandler {
public:
    explicit PointerStyleHandler(Prop<PointerStyle> style) : style_(std::move(style)) {}

    // Read when the pointer moves or the views change, not in an effect: a reactive style is
    // picked up at the next frame.
    PointerStyle pointer_style(Point) const override { return untrack([&] { return style_.get(); }); }

private:
    Prop<PointerStyle> style_;
};

} // namespace

std::unique_ptr<EventHandler> PointerStyleModifier::make_handler() const {
    return std::make_unique<PointerStyleHandler>(style_);
}

std::unique_ptr<EventHandler> MagnifyGestureModifier::make_handler() const {
    return std::make_unique<MagnifyHandler>(action_);
}

std::unique_ptr<EventHandler> RotateGestureModifier::make_handler() const {
    return std::make_unique<RotateHandler>(action_);
}

std::unique_ptr<EventHandler> SmartMagnifyModifier::make_handler() const {
    return std::make_unique<SmartMagnifyHandler>(action_);
}

std::unique_ptr<EventHandler> FocusableModifier::make_handler() const {
    return std::make_unique<detail::FocusableHandler>(focusable_);
}

std::unique_ptr<EventHandler> KeyPressModifier::make_handler() const {
    return std::make_unique<KeyPressHandler>(action_);
}

std::unique_ptr<EventHandler> TapGestureModifier::make_handler() const {
    return std::make_unique<TapHandler>(count_, action_);
}

std::unique_ptr<EventHandler> HoverModifier::make_handler() const {
    return std::make_unique<HoverHandler>(action_, delay_);
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
    if (uses_path()) {
        return hit_path(point);
    }
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

bool TransitionElement::hit_impl(Point point, std::vector<Element*>& path) {
    return !removing_ && content_->hit_test(point, path);
}

bool LayoutElement::hit_impl(Point point, std::vector<Element*>& path) {
    // Front to back, the reverse of drawing (exiting children never take hits).
    std::vector<Element*> order = laid_out_;
    std::ranges::stable_sort(order, {}, [](const Element* element) { return element->z_index(); });
    for (auto it = order.rbegin(); it != order.rend(); ++it) {
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
    // The content is drawn offset, so it's hit there too.
    point = {point.x - offset_x_.current(), point.y - offset_y_.current()};
    if (effects_.content_shape) {
        if (!inside(point, outline_of(*effects_.content_shape, frame()))) {
            return false;
        }
        content_->hit_test(point, path); // record what's under the pointer, if anything
        return true;
    }
    if (const ShapeFill* mask = clip_path(clip_scale_ > 0 ? clip_scale_ : 1.f)) {
        if (!shape_fill_contains(*mask, {point.x - frame().x, point.y - frame().y})) {
            return false;
        }
    } else if (effects_.clip_shape && !inside(point, outline_of(*effects_.clip_shape, frame()))) {
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

bool ButtonElement::hit_impl(Point point, std::vector<Element*>&) {
    return contains(frame(), point);
}

EventResult ButtonElement::pointer(const PointerEvent& event, Rect frame) {
    if (event.kind == PointerEvent::Kind::cancel) {
        down_ = false;
        pressed_ = false;
        return {};
    }
    if (event.button != MouseButton::left) {
        return {};
    }
    switch (event.kind) {
    case PointerEvent::Kind::down:
        if (!untrack(enabled_)) {
            return {.consumed = true}; // disabled buttons still block what's behind them
        }
        down_ = true;
        pressed_ = true;
        return {.consumed = true};
    case PointerEvent::Kind::move:
        // Looks pressed only while the pointer is over it, like native buttons.
        if (down_) {
            pressed_ = contains(frame, event.location);
        }
        return {.consumed = true};
    case PointerEvent::Kind::up: {
        const bool fire = down_ && contains(frame, event.location) && untrack(enabled_);
        down_ = false;
        pressed_ = false;
        if (fire && action_) {
            action_();
        }
        return {.consumed = true};
    }
    case PointerEvent::Kind::cancel:
    case PointerEvent::Kind::hover: break;
    }
    return {};
}

EventResult ButtonElement::hover(bool inside) {
    hovered_ = inside;
    if (on_hover_) {
        on_hover_(inside);
    }
    return {}; // the style's reactive arguments request the frame
}

bool ButtonElement::focusable() const {
    return untrack(enabled_);
}

// Like native buttons: Enter activates on press; Space looks pressed while held and activates on
// release.
EventResult ButtonElement::key(const KeyEvent& event) {
    if (extra_key_ != Key::unknown && event.key == extra_key_) {
        if (event.kind == KeyEvent::Kind::down && untrack(enabled_) && action_) {
            action_();
        }
        return {.consumed = true};
    }
    if (event.key != Key::space && event.key != Key::enter) {
        return {};
    }
    if (!untrack(enabled_)) {
        return {};
    }
    const bool down = event.kind == KeyEvent::Kind::down;
    if (event.key == Key::enter) {
        if (down && !event.repeat && action_) {
            action_();
        }
        return {.consumed = true};
    }
    if (down) {
        if (!event.repeat) {
            space_down_ = true;
            pressed_ = true;
        }
    } else if (std::exchange(space_down_, false)) {
        pressed_ = false;
        if (action_) {
            action_();
        }
    }
    return {.consumed = true};
}

EventResult ButtonElement::focus(bool focused) {
    if (!focused && std::exchange(space_down_, false)) {
        pressed_ = false;
    }
    if (focused_) {
        *focused_ = focused;
    }
    return {};
}

} // namespace elg::detail
