// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// ScrollView's element: layout, clipping, scroll bars, and wheel / drag / key scrolling.

#include "view/element.h"

#include <algorithm>

namespace elg::detail {

namespace {

constexpr float kLine = 40;              // arrow keys
constexpr float kRevealMargin = 8;       // around views scrolled into sight
constexpr float kBarInset = 2;           // scroll bar from the edge
constexpr float kBarThickness = 6;
constexpr float kMinThumb = 24;
constexpr float kThumbGrabSlop = 4;      // extra grab area around a thumb
constexpr float kIndicatorAlpha = 0.55f; // thumb opacity at full visibility

const Animation& wheel_animation() {
    static const Animation animation = Animation::ease_out(0.16);
    return animation;
}

bool contains(const Rect& rect, Point point) {
    return point.x >= rect.x && point.y >= rect.y && point.x < rect.max_x() && point.y < rect.max_y();
}

} // namespace

bool ScrollElement::scrolls(Axis axis) const noexcept {
    const auto bit = static_cast<int>(axis == Axis::vertical ? ScrollAxes::vertical : ScrollAxes::horizontal);
    return (static_cast<int>(options_.axes) & bit) != 0;
}

// Ideal size along the scroll axes; the offered size across them.
ProposedSize ScrollElement::content_proposal(ProposedSize proposal) const noexcept {
    return {scrolls(Axis::horizontal) ? std::nullopt : proposal.width,
            scrolls(Axis::vertical) ? std::nullopt : proposal.height};
}

Size ScrollElement::measure(ProposedSize proposal) {
    // Takes all it's offered (like SwiftUI); unspecified, it's as big as the content.
    return proposal.replacing_unspecified(content_->size_that_fits(content_proposal(proposal)));
}

Point ScrollElement::max_offset() const noexcept {
    return {scrolls(Axis::horizontal) ? std::max(0.f, content_size_.width - viewport_.width) : 0.f,
            scrolls(Axis::vertical) ? std::max(0.f, content_size_.height - viewport_.height) : 0.f};
}

void ScrollElement::arrange(Rect bounds, ProposedSize) {
    viewport_ = bounds;
    const ProposedSize inner = content_proposal({bounds.width, bounds.height});
    content_size_ = content_->size_that_fits(inner);

    // Stay in range when the content or the viewport changed size.
    const Point max = max_offset();
    if (target_.x > max.x || target_.y > max.y) {
        target_ = {std::min(target_.x, max.x), std::min(target_.y, max.y)};
        offset_x_.set(target_.x, nullptr);
        offset_y_.set(target_.y, nullptr);
    }

    // Starts at the top-leading corner along the scroll axes; centered across them.
    const Point offset = current_offset();
    const float x = scrolls(Axis::horizontal) ? bounds.x - offset.x
                                              : bounds.x + (bounds.width - content_size_.width) / 2;
    const float y = scrolls(Axis::vertical) ? bounds.y - offset.y
                                            : bounds.y + (bounds.height - content_size_.height) / 2;
    content_->place({x, y}, inner);
}

std::optional<ScrollElement::Thumb> ScrollElement::thumb(Axis axis) const {
    const bool vertical = axis == Axis::vertical;
    const float view = vertical ? viewport_.height : viewport_.width;
    const float content = vertical ? content_size_.height : content_size_.width;
    if (!options_.shows_indicators || !scrolls(axis) || content <= view + 0.5f) {
        return std::nullopt;
    }
    // Leave the corner free when both bars show.
    const bool other = axis == Axis::horizontal && thumb(Axis::vertical).has_value();
    const float track = view - 2 * kBarInset - (other ? kBarThickness + kBarInset : 0.f);
    const float length = std::clamp(track * view / content, std::min(kMinThumb, track), track);
    const float travel = track - length;
    const float max = vertical ? max_offset().y : max_offset().x;
    const float offset = vertical ? offset_y_.current() : offset_x_.current();
    const float position = max > 0 ? std::clamp(offset / max, 0.f, 1.f) * travel : 0.f;
    if (vertical) {
        return Thumb{{viewport_.max_x() - kBarInset - kBarThickness, viewport_.y + kBarInset + position,
                      kBarThickness, length},
                     travel};
    }
    return Thumb{{viewport_.x + kBarInset + position, viewport_.max_y() - kBarInset - kBarThickness, length,
                  kBarThickness},
                 travel};
}

std::optional<Axis> ScrollElement::thumb_at(Point point) const {
    if (!hovered_ && indicators_.current() < 0.05f) {
        return std::nullopt; // hidden bars don't take clicks from the content
    }
    for (const Axis axis : {Axis::vertical, Axis::horizontal}) {
        if (const std::optional<Thumb> t = thumb(axis); t && contains(t->rect.inset(-kThumbGrabSlop), point)) {
            return axis;
        }
    }
    return std::nullopt;
}

void ScrollElement::paint_impl(DisplayList& list) const {
    list.commands.push_back(PushClipCommand{frame(), {}});
    content_->paint(list);
    list.commands.push_back(PopClipCommand{});

    if (offset_x_.animating() || offset_y_.animating() || indicators_.animating()) {
        list.animating = true;
    }
    const float visibility = std::clamp(indicators_.current(), 0.f, 1.f);
    if (visibility <= 0) {
        return;
    }
    Color color = indicator_color_();
    color.a *= kIndicatorAlpha * visibility;
    for (const Axis axis : {Axis::vertical, Axis::horizontal}) {
        if (const std::optional<Thumb> t = thumb(axis)) {
            list.commands.push_back(
                RectCommand{t->rect, CornerRadii::all(kBarThickness / 2), ShapeStyle(color), Color::clear, 0});
        }
    }
}

bool ScrollElement::hit_impl(Point point, std::vector<Element*>& path) {
    if (!contains(frame(), point)) {
        return false; // clipped
    }
    if (!thumb_at(point)) {
        content_->hit_test(point, path);
    }
    return true; // empty space inside still scrolls
}

bool ScrollElement::scroll_to(Point target, const Animation* animation) {
    const Point max = max_offset();
    target = {std::clamp(target.x, 0.f, max.x), std::clamp(target.y, 0.f, max.y)};
    if (target.x == target_.x && target.y == target_.y) {
        return false;
    }
    target_ = target;
    offset_x_.set(target.x, animation);
    offset_y_.set(target.y, animation);
    return true;
}

void ScrollElement::show_indicators() {
    indicators_.set(1.f, nullptr);
}

// Fades the bars out after a pause, unless the pointer is on the view or dragging a bar.
void ScrollElement::fade_indicators() {
    if (hovered_ || dragging_) {
        return;
    }
    static const Animation fade = Animation::ease_out(0.4).delay(0.8);
    indicators_.set(0.f, &fade);
}

EventResult ScrollElement::scroll(const ScrollEvent& event) {
    float dx = event.dx;
    float dy = event.dy;
    if (options_.axes == ScrollAxes::horizontal && dx == 0) {
        std::swap(dx, dy); // a plain wheel scrolls a horizontal-only view
    }
    if (!scroll_to({target_.x + dx, target_.y + dy}, event.precise ? nullptr : &wheel_animation())) {
        return {}; // at the end: an outer scroll view may take it
    }
    show_indicators();
    fade_indicators();
    return {.consumed = true, .redraw = true};
}

EventResult ScrollElement::key(const KeyEvent& event) {
    if (event.kind != KeyEvent::Kind::down) {
        return {};
    }
    const float page = std::max(viewport_.height * 0.9f, kLine);
    Point target = target_;
    switch (event.key) {
    case Key::up: target.y -= kLine; break;
    case Key::down: target.y += kLine; break;
    case Key::page_up: target.y -= page; break;
    case Key::page_down: target.y += page; break;
    case Key::home: target.y = 0; break;
    case Key::end: target.y = max_offset().y; break;
    case Key::left: target.x -= kLine; break;
    case Key::right: target.x += kLine; break;
    default: return {};
    }
    static const Animation animation = Animation::ease_out(0.2);
    if (!scroll_to(target, &animation)) {
        return {};
    }
    show_indicators();
    fade_indicators();
    return {.consumed = true, .redraw = true};
}

EventResult ScrollElement::hover(bool inside) {
    hovered_ = inside;
    if (inside) {
        static const Animation appear = Animation::ease_out(0.15);
        indicators_.set(1.f, &appear);
    } else {
        fade_indicators();
    }
    return {.redraw = true};
}

EventResult ScrollElement::pointer(const PointerEvent& event, Rect) {
    switch (event.kind) {
    case PointerEvent::Kind::down:
        if (event.button != MouseButton::left) {
            return {};
        }
        dragging_ = thumb_at(event.location);
        if (!dragging_) {
            return {};
        }
        drag_start_ = event.location;
        drag_offset_ = target_;
        show_indicators();
        return {.consumed = true, .redraw = true};
    case PointerEvent::Kind::move: {
        if (!dragging_) {
            return {};
        }
        const std::optional<Thumb> t = thumb(*dragging_);
        if (!t || t->travel <= 0) {
            return {.consumed = true};
        }
        // The thumb follows the pointer: its travel maps to the whole scroll range.
        const Point max = max_offset();
        Point target = drag_offset_;
        if (*dragging_ == Axis::vertical) {
            target.y += (event.location.y - drag_start_.y) * max.y / t->travel;
        } else {
            target.x += (event.location.x - drag_start_.x) * max.x / t->travel;
        }
        scroll_to(target, nullptr);
        return {.consumed = true, .redraw = true};
    }
    case PointerEvent::Kind::up:
    case PointerEvent::Kind::cancel:
        if (!std::exchange(dragging_, std::nullopt)) {
            return {};
        }
        fade_indicators();
        return {.consumed = true, .redraw = true};
    }
    return {};
}

void ScrollElement::reveal(Rect rect) {
    // In content coordinates, as currently drawn.
    const Point offset = current_offset();
    const auto fit = [](float start, float length, float view, float current) {
        start -= kRevealMargin;
        length += 2 * kRevealMargin;
        if (start < current || length > view) {
            return start;
        }
        return start + length > current + view ? start + length - view : current;
    };
    Point target = target_;
    if (scrolls(Axis::vertical)) {
        target.y = fit(rect.y - viewport_.y + offset.y, rect.height, viewport_.height, target_.y);
    }
    if (scrolls(Axis::horizontal)) {
        target.x = fit(rect.x - viewport_.x + offset.x, rect.width, viewport_.width, target_.x);
    }
    static const Animation animation = Animation::ease_out(0.25);
    if (scroll_to(target, &animation)) {
        show_indicators();
        fade_indicators();
    }
}

} // namespace elg::detail
