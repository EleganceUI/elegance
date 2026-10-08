// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_VIEW_EVENT_DISPATCHER_H
#define ELG_VIEW_EVENT_DISPATCHER_H

#include "view/element.h"

#include <elg/events.h>

#include <vector>

namespace elg::detail {

// Routes one window's pointer input into its element tree (design.md §10):
// - down/up go to the handlers under the pointer, innermost first, until one consumes it;
//   consuming a down captures the pointer until the up;
// - hover enter/leave go to every handler under the pointer.
// Each method returns true when the window needs a new frame.
class EventDispatcher {
public:
    explicit EventDispatcher(Element& root) : root_(&root) {}

    bool pointer_moved(Point location);
    bool pointer_button(PointerEvent::Kind kind, Point location, MouseButton button, int click_count);
    bool pointer_left();
    // Call after If/ForEach removed elements: forgets elements that no longer exist (without
    // touching them) and re-evaluates hover at the last pointer position.
    bool revalidate();

private:
    std::vector<Element*> path_at(Point location);
    bool update_hover(const std::vector<Element*>& path);
    bool bubble(const std::vector<Element*>& path, const PointerEvent& event);

    Element* root_;
    std::vector<Element*> hovered_; // handler-bearing elements under the pointer, outermost first
    Element* captured_ = nullptr;
    Point last_location_;
    bool pointer_inside_ = false;
};

} // namespace elg::detail

#endif // ELG_VIEW_EVENT_DISPATCHER_H
