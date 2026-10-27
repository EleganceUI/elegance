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

EventResult EventHandler::scroll(const ScrollEvent&, Rect) {
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

class ClickHandler final : public EventHandler {
public:
    ClickHandler(std::function<void(const ClickEvent&)> action, ClickOptions options)
        : action_(std::move(action)), options_(options) {}

    EventResult pointer(const PointerEvent& event, Rect frame) override {
        if (event.kind == PointerEvent::Kind::cancel) {
            pressed_ = false;
            return {};
        }
        if (event.button != options_.button) {
            return {};
        }
        const int count = std::max(options_.count, 1);
        switch (event.kind) {
        case PointerEvent::Kind::down:
            // A double-click handler leaves first clicks to outer (single-click) handlers.
            if (event.click_count < count) {
                return {};
            }
            pressed_ = true;
            return {.consumed = true, .cancellable = true};
        case PointerEvent::Kind::up: {
            const bool fire = pressed_ && contains(frame, event.location) && event.click_count >= count &&
                              (event.click_count - count) % count == 0;
            pressed_ = false;
            if (fire && action_) {
                action_(ClickEvent{
                    .location = {event.location.x - frame.x, event.location.y - frame.y},
                    .window_location = event.location,
                    .button = event.button,
                    .modifiers = event.modifiers,
                    .click_count = event.click_count,
                });
            }
            return {.consumed = true};
        }
        case PointerEvent::Kind::cancel:
        case PointerEvent::Kind::move:
        case PointerEvent::Kind::hover: return {};
        }
        return {};
    }

private:
    std::function<void(const ClickEvent&)> action_;
    ClickOptions options_;
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

class KeyEventHandler final : public EventHandler {
public:
    explicit KeyEventHandler(std::function<bool(const KeyEvent&)> action) : action_(std::move(action)) {}

    EventResult key(const KeyEvent& event) override { return {.consumed = action_ && action_(event)}; }

private:
    std::function<bool(const KeyEvent&)> action_;
};

class PointerHandler final : public EventHandler {
public:
    explicit PointerHandler(std::function<bool(const PointerEvent&)> action) : action_(std::move(action)) {}

    EventResult pointer(const PointerEvent& event, Rect frame) override {
        if (!action_) {
            return {};
        }
        PointerEvent local = event;
        local.location = {event.location.x - frame.x, event.location.y - frame.y};
        return {.consumed = action_(local)};
    }

private:
    std::function<bool(const PointerEvent&)> action_;
};

class ScrollHandler final : public EventHandler {
public:
    explicit ScrollHandler(std::function<bool(const ScrollEvent&)> action) : action_(std::move(action)) {}

    EventResult scroll(const ScrollEvent& event, Rect frame) override {
        if (!action_) {
            return {};
        }
        ScrollEvent local = event;
        local.location = {event.location.x - frame.x, event.location.y - frame.y};
        return {.consumed = action_(local)};
    }

private:
    std::function<bool(const ScrollEvent&)> action_;
};

class TextInputHandler final : public EventHandler {
public:
    explicit TextInputHandler(std::function<bool(const TextInputEvent&)> action) : action_(std::move(action)) {}

    bool focusable() const override { return true; }
    bool accepts_text_input() const override { return true; }
    EventResult text_input(const TextInputEvent& event) override { return {.consumed = action_ && action_(event)}; }

private:
    std::function<bool(const TextInputEvent&)> action_;
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

std::unique_ptr<EventHandler> FocusableModifier::make_handler() const {
    return std::make_unique<detail::FocusableHandler>(focusable_);
}

std::unique_ptr<EventHandler> KeyEventModifier::make_handler() const {
    return std::make_unique<KeyEventHandler>(action_);
}

std::unique_ptr<EventHandler> PointerModifier::make_handler() const {
    return std::make_unique<PointerHandler>(action_);
}

std::unique_ptr<EventHandler> ScrollModifier::make_handler() const {
    return std::make_unique<ScrollHandler>(action_);
}

std::unique_ptr<EventHandler> TextInputModifier::make_handler() const {
    return std::make_unique<TextInputHandler>(action_);
}

std::unique_ptr<EventHandler> ClickModifier::make_handler() const {
    return std::make_unique<ClickHandler>(action_, options_);
}

std::unique_ptr<EventHandler> HoverModifier::make_handler() const {
    return std::make_unique<HoverHandler>(action_, delay_);
}

std::unique_ptr<EventHandler> PointerBoundaryModifier::make_handler() const {
    return std::make_unique<HoverHandler>(
        [enter = enter_, leave = leave_](bool inside) {
            if (inside && enter) {
                enter();
            } else if (!inside && leave) {
                leave();
            }
        },
        HoverDelay{.enter = 0, .exit = 0});
}

} // namespace elg

namespace elg::detail {

namespace {

bool contains(const Rect& rect, Point point) {
    return point.x >= rect.x && point.y >= rect.y && point.x < rect.max_x() && point.y < rect.max_y();
}

const ShapeSpec& spec_of(const Shape& shape) {
    return static_cast<const ShapeNode&>(*ViewAccess::node(shape)).spec();
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
    // Filled (or unstyled, which fills with the foreground): hit inside, like clip_shape and
    // content_shape of the same shape.
    if ((spec_.fill || !spec_.stroke) && hit_area_.contains(spec_, point, frame())) {
        return true;
    }
    if (!spec_.stroke) {
        return false;
    }
    if (uses_path()) {
        return hit_stroke_path(point);
    }
    // A stroke-only shape (e.g. a border overlay) is hit only on its line, as in SwiftUI, so it
    // doesn't block what it outlines.
    const ShapeOutline outline = shape_outline(spec_, frame());
    const ShapeStroke& stroke = *spec_.stroke;
    const float outer_grow = stroke.centered ? stroke.width / 2 : 0.f;
    const float inner_shrink = stroke.centered ? stroke.width / 2 : stroke.width;
    const auto adjust = [](const CornerRadii& radii, float delta) {
        const auto r = [&](float value) { return value > 0 ? std::max(0.f, value + delta) : 0.f; };
        return CornerRadii{r(radii.top_left), r(radii.top_right), r(radii.bottom_right), r(radii.bottom_left)};
    };
    const ShapeOutline outer{outline.rect.inset(-outer_grow), adjust(outline.radii, outer_grow)};
    const ShapeOutline inner{outline.rect.inset(inner_shrink), adjust(outline.radii, -inner_shrink)};
    return outline_contains(outer, point) && !outline_contains(inner, point);
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
        if (!content_area_.contains(spec_of(*effects_.content_shape), point, frame())) {
            return false;
        }
        content_->hit_test(point, path); // record what's under the pointer, if anything
        return true;
    }
    if (effects_.clip_shape && !clip_area_.contains(spec_of(*effects_.clip_shape), point, frame())) {
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
        return {.consumed = true, .cancellable = true};
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
