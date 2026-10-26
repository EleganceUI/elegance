// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// ScrollView's element: layout, clipping, scroll bars, and wheel / drag / key scrolling.

#include "view/element.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <numbers>

namespace elg::detail {

namespace {

constexpr float kLine = 40;              // arrow keys
constexpr float kRevealMargin = 8;       // around views scrolled into sight
constexpr float kBarInset = 2;           // scroll bar from the edge
constexpr float kBarThickness = 6;
constexpr float kMinThumb = 24;
constexpr float kThumbGrabSlop = 4;      // extra grab area around a thumb
constexpr float kIndicatorAlpha = 0.55f; // thumb opacity at full visibility
constexpr float kStretch = 0.55f;        // resistance past the edges (UIScrollView's)
constexpr double kReboundRate = 15;      // spring rate (1/s): back at an edge in about 0.4 s
constexpr float kMaxBounce = 0.08f;      // how far a coast bounces past an edge, of the view
constexpr double kDeceleration = 2;      // a coast's velocity falls by e every 1/2 s (UIScrollView's
                                         // normal rate: 0.998 per millisecond)
constexpr float kMinCoast = 60;          // slower than this (points per second), letting go stops
constexpr double kFingerRest = 0.06;     // fingers still this long before lifting: no coast
constexpr double kVelocityWindow = 0.1;  // the fingers' speed is their average over this long

const Animation& wheel_animation() {
    static const Animation animation = Animation::ease_out(0.16);
    return animation;
}

bool contains(const Rect& rect, Point point) {
    return point.x >= rect.x && point.y >= rect.y && point.x < rect.max_x() && point.y < rect.max_y();
}

double now_seconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// Past an edge the content follows the fingers less and less: `beyond` points of travel show as
// this much, approaching `extent` (the view's length).
float resist(float beyond, float extent) {
    return beyond * kStretch * extent / (beyond * kStretch + extent);
}

float unresist(float shown, float extent) {
    shown = std::min(shown, extent * 0.99f);
    return shown * extent / (kStretch * (extent - shown));
}

// Finger travel to where the offset shows, and back, for an axis scrolling 0...max.
float stretched(float travel, float max, float extent) {
    if (travel < 0) {
        return -resist(-travel, extent);
    }
    return travel > max ? max + resist(travel - max, extent) : travel;
}

float unstretched(float shown, float max, float extent) {
    if (shown < 0) {
        return -unresist(-shown, extent);
    }
    return shown > max ? max + unresist(shown - max, extent) : shown;
}

} // namespace

float ScrollElement::Rebound::at(double time) const {
    const double t = std::max(0.0, time - start);
    return edge + static_cast<float>((displacement + (velocity + kReboundRate * displacement) * t) *
                                     std::exp(-kReboundRate * t));
}

bool ScrollElement::Rebound::settled(double time) const {
    return time - start > 7 / kReboundRate; // within 1% of where it started from the edge
}

float ScrollElement::Coast::at(double time) const {
    const double t = std::max(0.0, time - start);
    return from + static_cast<float>(velocity / kDeceleration * (1 - std::exp(-kDeceleration * t)));
}

float ScrollElement::Coast::velocity_at(double time) const {
    return velocity * static_cast<float>(std::exp(-kDeceleration * std::max(0.0, time - start)));
}

float ScrollElement::Coast::end() const {
    return from + static_cast<float>(velocity / kDeceleration);
}

float ScrollElement::offset(Axis axis) const {
    const Rebound& rebound = axis == Axis::vertical ? rebound_y_ : rebound_x_;
    if (rebound.active) {
        return rebound.at(frame_time());
    }
    if (const Coast& coast = axis == Axis::vertical ? coast_y_ : coast_x_; coast.active) {
        return coast.at(frame_time());
    }
    return axis == Axis::vertical ? offset_y_.current() : offset_x_.current();
}

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

    // A coast stops when it's nearly still, or bounces off an edge it reaches.
    const Point range = max_offset();
    for (const Axis axis : {Axis::horizontal, Axis::vertical}) {
        const bool vertical = axis == Axis::vertical;
        Coast& coast = vertical ? coast_y_ : coast_x_;
        if (!coast.active) {
            continue;
        }
        const double now = frame_time();
        const float position = coast.at(now);
        const float velocity = coast.velocity_at(now);
        const float max = vertical ? range.y : range.x;
        if (position < 0 || position > max) {
            const float edge = position < 0 ? 0.f : max;
            const float extent = vertical ? viewport_.height : viewport_.width;
            // A critically damped spring launched at v peaks at v / (rate * e).
            const float limit = kMaxBounce * extent * static_cast<float>(kReboundRate * std::numbers::e);
            (vertical ? rebound_y_ : rebound_x_) = {.start = now,
                                                    .edge = edge,
                                                    .displacement = position - edge,
                                                    .velocity = std::clamp(velocity, -limit, limit),
                                                    .active = true};
            coast.active = false;
        } else if (std::abs(velocity) < kMinCoast / 4) {
            coast.active = false;
            (vertical ? offset_y_ : offset_x_).set(position, nullptr);
            (vertical ? target_.y : target_.x) = position;
        }
    }

    // Rebounds that have settled hand the offset back.
    for (Rebound* rebound : {&rebound_x_, &rebound_y_}) {
        if (rebound->active && rebound->settled(frame_time())) {
            rebound->active = false;
            (rebound == &rebound_y_ ? offset_y_ : offset_x_).set(rebound->edge, nullptr);
            (rebound == &rebound_y_ ? target_.y : target_.x) = rebound->edge;
        }
    }

    // Stay in range when the content or the viewport changed size (not while stretched past it).
    const Point max = max_offset();
    if (!stretching() && (target_.x > max.x || target_.y > max.y)) {
        target_ = {std::min(target_.x, max.x), std::min(target_.y, max.y)};
        offset_x_.set(target_.x, nullptr);
        offset_y_.set(target_.y, nullptr);
    }

    // Following the end (pinned_): it moved, so go with it. Scrolling up unpins (scroll_to).
    if (options_.sticks_to_end && pinned_ && !stretching() && scrolls(Axis::vertical) && target_.y != max.y) {
        target_.y = max.y;
        offset_y_.set(target_.y, nullptr);
    }

    apply_request(); // a ScrollPosition call, now that sizes are known

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
    const float full = std::clamp(track * view / content, std::min(kMinThumb, track), track);
    const float travel = track - full;
    const float max = vertical ? max_offset().y : max_offset().x;
    const float offset = this->offset(axis);
    // Stretched past an end, the thumb squeezes against it, like the content.
    const float beyond = offset < 0 ? -offset : std::max(offset - max, 0.f);
    const float length = std::max(full - beyond * track / view, std::min(kBarThickness * 2, full));
    const float position = offset < 0 ? 0.f : offset > max ? track - length
                         : max > 0    ? offset / max * travel
                                      : 0.f;
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
    // Only what's in the viewport is painted.
    const std::optional<Rect> outer = list.cull;
    Rect visible = frame();
    if (outer) {
        const float x = std::max(visible.x, outer->x);
        const float y = std::max(visible.y, outer->y);
        visible = {x, y, std::max(0.f, std::min(visible.max_x(), outer->max_x()) - x),
                   std::max(0.f, std::min(visible.max_y(), outer->max_y()) - y)};
    }
    list.cull = visible;
    content_->paint(list);
    list.cull = outer;
    list.commands.push_back(PopClipCommand{});

    if (offset_x_.animating() || offset_y_.animating() || indicators_.animating() || stretching()) {
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
    stop_motion();
    target_ = target;
    offset_x_.set(target.x, animation);
    offset_y_.set(target.y, animation);
    pinned_ = target.y >= max.y - 0.5f; // at the end: follow it as the content grows
    return true;
}

bool ScrollElement::anchor_shift(Axis axis, float delta) {
    // At the start, rows inserted before the first one should come into view, not push it down
    // (browsers skip anchoring there too).
    if (!scrolls(axis) || (axis == Axis::vertical ? target_.y : target_.x) <= 0.5f) {
        return false;
    }
    if (axis == Axis::vertical) {
        if (options_.sticks_to_end && pinned_) {
            return false; // following the end instead
        }
        target_.y += delta;
        offset_y_.shift(delta);
    } else {
        target_.x += delta;
        offset_x_.shift(delta);
    }
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
    return event.phase == GesturePhase::none ? wheel(event) : gesture(event);
}

// Points per second over the fingers' last movements; 0 if they rested before lifting at `now`.
float ScrollElement::finger_velocity(double now) const {
    if (moves_.empty() || now - moves_.back().time > kFingerRest) {
        return 0;
    }
    // Each delta covers the time since the event before it: the first one's started earlier.
    float distance = 0;
    for (std::size_t i = 1; i < moves_.size(); ++i) {
        distance += moves_[i].delta;
    }
    const double span = moves_.back().time - moves_.front().time;
    if (moves_.size() < 2 || span < 0.008) {
        return 0; // too few to tell (a tap of a scroll)
    }
    return static_cast<float>(distance / span);
}

void ScrollElement::stop_motion() {
    coast_x_.active = false;
    coast_y_.active = false;
    rebound_x_.active = false;
    rebound_y_.active = false;
}

EventResult ScrollElement::gesture(const ScrollEvent& event) {
    // The system's momentum after a flick: the view coasts on its own instead (it knows the
    // fingers' speed and the edges), so it glides the same wherever the momentum comes from.
    if (event.momentum) {
        return {.consumed = lock_.has_value() && scrolls(*lock_)};
    }
    const Point max = max_offset();
    if (event.phase == GesturePhase::began) {
        // Fingers down: stop wherever it is (coasting, bouncing, easing) and follow them from there.
        const Point at = current_offset();
        stop_motion();
        offset_x_.set(at.x, nullptr);
        offset_y_.set(at.y, nullptr);
        target_ = at;
        stretch_ = {unstretched(at.x, max.x, viewport_.width), unstretched(at.y, max.y, viewport_.height)};
        lock_.reset();
        touching_ = false;
        moves_.clear();
        if (event.dx == 0 && event.dy == 0) {
            return {.redraw = true}; // not consumed: whatever else is under the fingers stops too
        }
    }
    if (!lock_) {
        if (event.dx == 0 && event.dy == 0) {
            return {};
        }
        // Like AppKit's predominant-axis scrolling: the gesture follows the axis it started along.
        lock_ = std::abs(event.dx) > std::abs(event.dy) ? Axis::horizontal : Axis::vertical;
    }
    const Axis axis = *lock_;
    const bool vertical = axis == Axis::vertical;
    const float range = vertical ? max.y : max.x;
    if (!scrolls(axis) || range <= 0) {
        return {}; // an outer scroll view's
    }
    const float extent = vertical ? viewport_.height : viewport_.width;
    float& shown = vertical ? target_.y : target_.x;
    Animated<float>& animated = vertical ? offset_y_ : offset_x_;
    Rebound& rebound = vertical ? rebound_y_ : rebound_x_;
    Coast& coast = vertical ? coast_y_ : coast_x_;
    const auto show = [&](float value) {
        shown = value;
        animated.set(value, nullptr);
        if (vertical) {
            pinned_ = value >= range - 0.5f;
        }
    };
    const float delta = vertical ? event.dy : event.dx;

    const double now = event.time > 0 ? event.time : now_seconds();
    if (event.phase == GesturePhase::ended || event.phase == GesturePhase::cancelled) {
        touching_ = false;
        const float edge = std::clamp(shown, 0.f, range);
        if (shown != edge) {
            // Let go past an edge: spring back to it.
            rebound = {.start = frame_time(), .edge = edge, .displacement = shown - edge, .active = true};
        } else if (const float velocity = finger_velocity(now); std::abs(velocity) >= kMinCoast) {
            // Let go while moving: glide on at the fingers' speed, slowing down.
            coast = {.start = frame_time(), .from = shown, .velocity = velocity, .active = true};
            shown = std::clamp(coast.end(), 0.f, range);
        }
        fade_indicators();
        return {.consumed = true, .redraw = true};
    }
    std::erase_if(moves_, [&](const Move& move) { return move.time < now - kVelocityWindow; });
    moves_.push_back({now, delta});
    touching_ = true;
    float& travel = vertical ? stretch_.y : stretch_.x;
    travel += delta;
    show(stretched(travel, range, extent));
    show_indicators();
    return {.consumed = true, .redraw = true};
}

EventResult ScrollElement::wheel(const ScrollEvent& event) {
    float dx = event.dx;
    float dy = event.dy;
    // A mouse wheel's notches scroll a horizontal-only view sideways. It only gets them when a
    // gesture starts over it: the dispatcher latches gestures, so scrolling a page past it keeps
    // scrolling the page. Trackpads scroll sideways themselves; their vertical part goes on.
    if (options_.axes == ScrollAxes::horizontal && dx == 0 && !event.precise) {
        std::swap(dx, dy);
    }
    dx = scrolls(Axis::horizontal) ? dx : 0.f;
    dy = scrolls(Axis::vertical) ? dy : 0.f;
    if ((dx == 0 && dy == 0) ||
        !scroll_to({target_.x + dx, target_.y + dy}, event.precise ? nullptr : &wheel_animation())) {
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
    case PointerEvent::Kind::hover: return {};
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

void ScrollElement::apply_request() {
    if (!options_.position) {
        return;
    }
    std::optional<ScrollRequest>& request = ScrollTargetAccess::target(*options_.position)->request;
    if (!request) {
        return;
    }
    static const Animation animation = Animation::smooth(0.45);
    const Animation* how = request->animated ? &animation : nullptr;
    switch (request->kind) {
    case ScrollRequest::Kind::top:
        scroll_to({target_.x, 0}, how);
        request.reset();
        break;
    case ScrollRequest::Kind::end:
        scroll_to({target_.x, max_offset().y}, how);
        request.reset();
        break;
    case ScrollRequest::Kind::item: {
        // Find the lazy stack holding the item; its offset is relative to its own frame.
        std::optional<float> found;
        const Element* stack = nullptr;
        std::function<void(Element&)> walk = [&](Element& element) {
            if (found) {
                return;
            }
            if (const std::optional<float> offset = element.item_offset(request->key)) {
                found = offset;
                stack = &element;
                return;
            }
            element.for_each_child(walk);
        };
        content_->for_each_child(walk);
        if (!found) {
            walk(*content_);
        }
        if (found) {
            // Content coordinates: where the stack is now, plus the item's offset in it.
            const float y = stack->frame().y - viewport_.y + offset_y_.current() + *found;
            scroll_to({target_.x, y}, nullptr);
        }
        // Items above it get measured as they're built, moving it: aim again for a few layouts.
        if (!found || --request->settle <= 0) {
            request.reset();
        }
        break;
    }
    }
    show_indicators();
    fade_indicators();
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
