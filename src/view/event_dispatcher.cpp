// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include "view/event_dispatcher.h"

#include <algorithm>
#include <functional>
#include <unordered_set>

namespace elg::detail {

std::vector<Element*> EventDispatcher::path_at(Point location) {
    std::vector<Element*> path;
    root_->hit_test(location, path);
    return path;
}

bool EventDispatcher::update_hover(const std::vector<Element*>& path) {
    std::vector<Element*> now;
    for (Element* element : path) {
        if (element->event_handler()) {
            now.push_back(element);
        }
    }

    bool redraw = false;
    // Leaves innermost first, then enters outermost first, like the DOM's mouseleave/enter.
    for (auto it = hovered_.rbegin(); it != hovered_.rend(); ++it) {
        if (std::ranges::find(now, *it) == now.end()) {
            redraw |= (*it)->event_handler()->hover(false).redraw;
        }
    }
    for (Element* element : now) {
        if (std::ranges::find(hovered_, element) == hovered_.end()) {
            redraw |= element->event_handler()->hover(true).redraw;
        }
    }
    hovered_ = std::move(now);
    return redraw;
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
    std::erase_if(hovered_, [&](const Element* element) { return !alive.contains(element); });
    if (captured_ && !alive.contains(captured_)) {
        captured_ = nullptr;
    }
    return pointer_inside_ ? update_hover(path_at(last_location_)) : false;
}

} // namespace elg::detail
