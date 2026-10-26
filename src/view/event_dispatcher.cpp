// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include "view/event_dispatcher.h"

#include "view/window_drag.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <unordered_set>

namespace elg::detail {

// --- FocusManager ---

bool FocusManager::focus(Element* element, bool visible, bool navigated) {
    navigated_ = navigated;
    if (element == focused_) {
        return std::exchange(visible_, visible) != visible;
    }
    Element* const previous = std::exchange(focused_, element);
    visible_ = visible;
    set_text_input(element && element->event_handler()->accepts_text_input());
    // focused_ is already updated, so handlers (e.g. a .focused binding) see the new focus.
    if (previous) {
        previous->event_handler()->focus(false);
    }
    if (element) {
        element->event_handler()->focus(true);
    }
    notify_watches();
    return true;
}

bool FocusManager::contains_focus(const Element* scope) const noexcept {
    for (const Element* element = focused_; element; element = element->parent()) {
        if (element == scope) {
            return true;
        }
    }
    return false;
}

Element* FocusManager::first_focusable(Element& scope) {
    Element* found = nullptr;
    std::function<void(Element&)> walk = [&](Element& element) {
        if (found || blocks_focus(element)) {
            return;
        }
        if (EventHandler* handler = element.event_handler(); handler && handler->focusable()) {
            found = &element;
            return;
        }
        element.for_each_child(walk);
    };
    scope.for_each_child(walk);
    if (!found) {
        if (EventHandler* handler = scope.event_handler(); handler && handler->focusable()) {
            found = &scope;
        }
    }
    return found;
}

void FocusManager::watch(Element* scope, std::function<void(bool)> changed) {
    watches_.push_back({scope, std::move(changed), contains_focus(scope)});
}

void FocusManager::notify_watches() {
    // Copied first: a callback's state change may add or remove watches.
    std::vector<std::pair<std::function<void(bool)>, bool>> calls;
    for (Watch& watch : watches_) {
        const bool within = contains_focus(watch.scope);
        if (within != watch.within) {
            watch.within = within;
            calls.emplace_back(watch.changed, within);
        }
    }
    for (const auto& [changed, within] : calls) {
        changed(within);
    }
}

void FocusManager::forget(const Element* element) noexcept {
    std::erase_if(watches_, [&](const Watch& watch) { return watch.scope == element; });
    if (focused_ == element) {
        focused_ = nullptr;
        set_text_input(false);
        try {
            notify_watches();
        } catch (...) {
            // A binding that throws while the tree is torn down has nowhere to report to.
        }
    }
}

// --- EventDispatcher ---

std::vector<Element*> EventDispatcher::path_at(Point location) {
    std::vector<Element*> path;
    root_->hit_test(location, path);
    return path;
}

namespace {

// The event as `element` sees it: in its layout's coordinates. Content inside .offset is drawn
// (and hit) away from where it's laid out, so the pointer is moved back by those offsets.
PointerEvent local_event(PointerEvent event, const Element& element) {
    const Rect drawn = element.drawn_frame();
    const Rect laid_out = element.frame();
    event.location.x -= drawn.x - laid_out.x;
    event.location.y -= drawn.y - laid_out.y;
    return event;
}

// Delivers `event` to `element`'s handler, in its coordinates.
EventResult deliver(const PointerEvent& event, Element& element) {
    return element.event_handler()->pointer(local_event(event, element), element.frame());
}

double now_seconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

} // namespace

bool EventDispatcher::update_hover(const std::vector<Element*>& path) {
    const double now = now_seconds();
    const auto under_pointer = [&](const Element* element) { return std::ranges::find(path, element) != path.end(); };
    // Schedules telling the handler about a change, or cancels a pending one that was undone
    // (e.g. the pointer left before the enter delay passed).
    const auto schedule = [&](Hover& hover) {
        if (hover.over == hover.reported) {
            hover.due = -1;
        } else if (hover.due < 0) {
            const HoverDelay delay = hover.element->effective_hover_delay();
            hover.due = now + (hover.over ? delay.enter : delay.exit);
        }
    };

    for (Hover& hover : hovers_) {
        hover.over = under_pointer(hover.element);
        schedule(hover);
    }
    for (Element* element : path) {
        if (element->event_handler() &&
            std::ranges::find(hovers_, element, &Hover::element) == hovers_.end()) {
            Hover hover{element, true, false, -1};
            schedule(hover);
            hovers_.push_back(hover);
        }
    }
    return tick();
}

bool EventDispatcher::tick() {
    const double now = now_seconds();
    bool redraw = false;
    // Leaves innermost first, then enters outermost first, like the DOM's mouseleave/enter.
    for (auto it = hovers_.rbegin(); it != hovers_.rend(); ++it) {
        if (it->due >= 0 && it->due <= now && !it->over) {
            it->reported = false;
            it->due = -1;
            redraw |= it->element->event_handler()->hover(false).redraw;
        }
    }
    for (Hover& hover : hovers_) {
        if (hover.due >= 0 && hover.due <= now && hover.over) {
            hover.reported = true;
            hover.due = -1;
            redraw |= hover.element->event_handler()->hover(true).redraw;
        }
    }
    // Forget elements the pointer is off that have nothing left to hear.
    std::erase_if(hovers_, [](const Hover& hover) { return !hover.over && !hover.reported && hover.due < 0; });
    return redraw;
}

std::optional<double> EventDispatcher::next_deadline() const {
    std::optional<double> next;
    for (const Hover& hover : hovers_) {
        if (hover.due >= 0 && (!next || hover.due < *next)) {
            next = hover.due;
        }
    }
    return next;
}

bool EventDispatcher::bubble(const std::vector<Element*>& path, const PointerEvent& event) {
    bool redraw = false;
    for (auto it = path.rbegin(); it != path.rend(); ++it) {
        EventHandler* handler = (*it)->event_handler();
        if (!handler) {
            continue;
        }
        const EventResult result = deliver(event, **it);
        redraw |= result.redraw;
        if (result.consumed) {
            if (event.kind == PointerEvent::Kind::down) {
                captured_ = *it;
            }
            break;
        }
    }
    return redraw;
}

bool EventDispatcher::pointer_moved(Point location) {
    // Moving the mouse ends a wheel gesture, so the next one goes to what's under it. Ignored
    // right after a wheel event, when a hand on the wheel jiggles the mouse.
    constexpr double kWheelMoveGrace = 0.1;
    constexpr float kWheelMoveSlop = 4;
    if (wheel_latched_ && !wheel_phased_ && now_seconds() - last_wheel_ > kWheelMoveGrace &&
        std::hypot(location.x - last_wheel_location_.x, location.y - last_wheel_location_.y) > kWheelMoveSlop) {
        wheel_latched_ = nullptr;
    }
    last_location_ = location;
    pointer_inside_ = true;
    const std::vector<Element*> path = path_at(location);
    bool redraw = update_hover(path);
    if (captured_) {
        const PointerEvent event{PointerEvent::Kind::move, location};
        redraw |= deliver(event, *captured_).redraw;
    } else {
        redraw |= bubble(path, PointerEvent{PointerEvent::Kind::hover, location});
    }
    return redraw;
}

bool EventDispatcher::pointer_button(PointerEvent::Kind kind, Point location, MouseButton button,
                                     int click_count, KeyModifiers modifiers) {
    last_location_ = location;
    pointer_inside_ = true;
    const PointerEvent event{kind, location, button, click_count, modifiers};
    const std::vector<Element*> path = path_at(location);
    bool redraw = update_hover(path);

    if (kind == PointerEvent::Kind::down) {
        // Clicking focuses what was clicked (without a ring) or, on nothing focusable, clears focus.
        Element* target = nullptr;
        for (auto it = path.rbegin(); it != path.rend(); ++it) {
            if (EventHandler* handler = (*it)->event_handler(); handler && handler->focusable()) {
                target = *it;
                break;
            }
        }
        // Inside a .focusable(false) scope (e.g. caption buttons) a click leaves focus alone.
        if (!std::ranges::any_of(path, [](Element* element) { return blocks_focus(*element); })) {
            redraw |= focus_->focus(target, false);
        }
    }
    if (kind == PointerEvent::Kind::up && captured_) {
        Element* target = std::exchange(captured_, nullptr);
        return redraw | deliver(event, *target).redraw;
    }
    return redraw | bubble(path, event);
}

bool EventDispatcher::pointer_left() {
    // While a button is held the platform keeps the pointer captured, so leaving only matters
    // for hover.
    pointer_inside_ = false;
    return update_hover({});
}

bool EventDispatcher::key(const KeyEvent& event) {
    bool redraw = false;
    for (Element* element = focus_->focused(); element; element = element->parent()) {
        if (EventHandler* handler = element->event_handler()) {
            const EventResult result = handler->key(event);
            redraw |= result.redraw;
            if (result.consumed) {
                return redraw;
            }
        }
    }
    const bool plain_tab = event.kind == KeyEvent::Kind::down && event.key == Key::tab &&
                           !has(event.modifiers, KeyModifiers::control | KeyModifiers::alt | KeyModifiers::super);
    if (plain_tab) {
        redraw |= move_focus(!has(event.modifiers, KeyModifiers::shift));
    }
    if (menu_navigation_ && event.kind == KeyEvent::Kind::down) {
        switch (event.key) {
        case Key::down: redraw |= move_focus(true); break;
        case Key::up: redraw |= move_focus(false); break;
        case Key::home: redraw |= focus_edge(true); break;
        case Key::end: redraw |= focus_edge(false); break;
        default: break;
        }
    }
    return redraw;
}

bool EventDispatcher::focus_edge(bool first) {
    std::vector<Element*> order;
    std::function<void(Element&)> walk = [&](Element& element) {
        if (blocks_focus(element)) {
            return;
        }
        if (EventHandler* handler = element.event_handler(); handler && handler->focusable()) {
            order.push_back(&element);
        }
        element.for_each_child(walk);
    };
    walk(*root_);
    if (order.empty()) {
        return false;
    }
    return focus_->focus(first ? order.front() : order.back(), true, true);
}

bool EventDispatcher::move_focus(bool forward) {
    std::vector<Element*> order;
    std::function<void(Element&)> walk = [&](Element& element) {
        if (blocks_focus(element)) {
            return;
        }
        if (EventHandler* handler = element.event_handler(); handler && handler->focusable()) {
            order.push_back(&element);
        }
        element.for_each_child(walk);
    };
    walk(*root_);
    if (order.empty()) {
        return false;
    }
    const auto current = std::ranges::find(order, focus_->focused());
    Element* next;
    if (current == order.end()) {
        next = forward ? order.front() : order.back();
    } else {
        const std::size_t index = static_cast<std::size_t>(current - order.begin());
        next = order[(index + (forward ? 1 : order.size() - 1)) % order.size()];
    }
    const bool redraw = focus_->focus(next, true, true);
    for (Element* ancestor = next->parent(); ancestor; ancestor = ancestor->parent()) {
        ancestor->reveal(next->frame());
    }
    return redraw;
}

bool EventDispatcher::scroll(const ScrollEvent& event) {
    // A wheel gesture ends after this long without wheel events.
    constexpr double kWheelGesturePause = 0.5;
    const Point location = event.location;
    last_location_ = location;
    pointer_inside_ = true;
    const double now = now_seconds();
    const bool phased = event.phase != GesturePhase::none;
    // Fingers touching down start a new gesture; until they move, every scroll view under them
    // hears it (to stop a bounce), and the first to take a delta keeps the gesture.
    if (phased ? event.phase == GesturePhase::began && !event.momentum
               : wheel_phased_ || now - last_wheel_ > kWheelGesturePause) {
        wheel_latched_ = nullptr;
    }
    wheel_phased_ = phased;
    last_wheel_ = now;
    last_wheel_location_ = location;

    // Latched: to that element and its ancestors, whatever is under the pointer now. Otherwise
    // the innermost element under the pointer first, and whichever takes it is latched.
    std::vector<Element*> path;
    if (wheel_latched_) {
        for (Element* element = wheel_latched_; element; element = element->parent()) {
            path.insert(path.begin(), element);
        }
    } else {
        path = path_at(location);
    }
    bool redraw = false;
    for (auto it = path.rbegin(); it != path.rend(); ++it) {
        if (EventHandler* handler = (*it)->event_handler()) {
            const EventResult result = handler->scroll(event);
            redraw |= result.redraw;
            if (result.consumed) {
                if (!wheel_latched_) {
                    wheel_latched_ = *it;
                }
                break;
            }
        }
    }
    return redraw;
}

namespace {

// Pinch and rotate: `began` goes to the innermost handler under the pointer that takes it, the rest
// of the gesture to that one.
template <class Event, class Deliver>
bool gesture(Element*& latched, const std::vector<Element*>& path, const Event& event, Deliver deliver) {
    bool redraw = false;
    if (event.phase == GesturePhase::began || event.phase == GesturePhase::none) {
        latched = nullptr;
        for (auto it = path.rbegin(); it != path.rend(); ++it) {
            if (EventHandler* handler = (*it)->event_handler()) {
                const EventResult result = deliver(*handler, event);
                redraw |= result.redraw;
                if (result.consumed) {
                    latched = *it;
                    break;
                }
            }
        }
    } else if (latched) {
        if (EventHandler* handler = latched->event_handler()) {
            redraw = deliver(*handler, event).redraw;
        }
    }
    if (event.phase == GesturePhase::ended || event.phase == GesturePhase::cancelled) {
        latched = nullptr;
    }
    return redraw;
}

} // namespace

bool EventDispatcher::magnify(MagnifyEvent event) {
    magnify_scale_ = event.phase == GesturePhase::began ? event.delta : magnify_scale_ * event.delta;
    event.scale = magnify_scale_;
    return gesture(magnify_latched_, path_at(event.location), event,
                   [](EventHandler& handler, const MagnifyEvent& e) { return handler.magnify(e); });
}

bool EventDispatcher::rotate(RotateEvent event) {
    rotate_degrees_ = event.phase == GesturePhase::began ? event.delta : rotate_degrees_ + event.delta;
    event.degrees = rotate_degrees_;
    return gesture(rotate_latched_, path_at(event.location), event,
                   [](EventHandler& handler, const RotateEvent& e) { return handler.rotate(e); });
}

bool EventDispatcher::smart_magnify(Point location) {
    bool redraw = false;
    const std::vector<Element*> path = path_at(location);
    for (auto it = path.rbegin(); it != path.rend(); ++it) {
        if (EventHandler* handler = (*it)->event_handler()) {
            const EventResult result = handler->smart_magnify(location);
            redraw |= result.redraw;
            if (result.consumed) {
                break;
            }
        }
    }
    return redraw;
}

bool EventDispatcher::text_input(const TextInputEvent& event) {
    bool redraw = false;
    for (Element* element = focus_->focused(); element; element = element->parent()) {
        if (EventHandler* handler = element->event_handler()) {
            const EventResult result = handler->text_input(event);
            redraw |= result.redraw;
            if (result.consumed) {
                break;
            }
        }
    }
    return redraw;
}

bool EventDispatcher::drags_window_at(Point location) {
    const std::vector<Element*> path = path_at(location);
    for (auto it = path.rbegin(); it != path.rend(); ++it) {
        if (EventHandler* handler = (*it)->event_handler()) {
            const auto* drag = dynamic_cast<const WindowDragHandler*>(handler);
            return drag && drag->enabled();
        }
    }
    return false;
}

bool EventDispatcher::refresh_hover() {
    return pointer_inside_ && update_hover(path_at(last_location_));
}

bool EventDispatcher::revalidate() {
    std::unordered_set<const Element*> alive;
    std::function<void(Element&)> walk = [&](Element& element) {
        alive.insert(&element);
        element.for_each_child(walk);
    };
    walk(*root_);

    // Pointers to removed elements are compared, never dereferenced.
    std::erase_if(hovers_, [&](const Hover& hover) { return !alive.contains(hover.element); });
    if (captured_ && !alive.contains(captured_)) {
        captured_ = nullptr;
    }
    for (Element** latched : {&wheel_latched_, &magnify_latched_, &rotate_latched_}) {
        if (*latched && !alive.contains(*latched)) {
            *latched = nullptr;
        }
    }
    return pointer_inside_ ? update_hover(path_at(last_location_)) : false;
}

} // namespace elg::detail
