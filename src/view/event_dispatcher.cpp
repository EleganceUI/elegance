// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include "view/event_dispatcher.h"

#include <algorithm>
#include <chrono>
#include <functional>
#include <unordered_set>

namespace elg::detail {

std::vector<Element*> EventDispatcher::path_at(Point location) {
    std::vector<Element*> path;
    root_->hit_test(location, path);
    return path;
}

namespace {

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
        const EventResult result = handler->pointer(event, (*it)->frame());
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
    last_location_ = location;
    pointer_inside_ = true;
    bool redraw = update_hover(path_at(location));
    if (captured_) {
        const PointerEvent event{PointerEvent::Kind::move, location};
        redraw |= captured_->event_handler()->pointer(event, captured_->frame()).redraw;
    }
    return redraw;
}

bool EventDispatcher::pointer_button(PointerEvent::Kind kind, Point location, MouseButton button,
                                     int click_count) {
    last_location_ = location;
    pointer_inside_ = true;
    const PointerEvent event{kind, location, button, click_count};
    const std::vector<Element*> path = path_at(location);
    bool redraw = update_hover(path);

    if (kind == PointerEvent::Kind::up && captured_) {
        Element* target = std::exchange(captured_, nullptr);
        return redraw | target->event_handler()->pointer(event, target->frame()).redraw;
    }
    return redraw | bubble(path, event);
}

bool EventDispatcher::pointer_left() {
    // While a button is held the platform keeps the pointer captured, so leaving only matters
    // for hover.
    pointer_inside_ = false;
    return update_hover({});
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
    return pointer_inside_ ? update_hover(path_at(last_location_)) : false;
}

} // namespace elg::detail
