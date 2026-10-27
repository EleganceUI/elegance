// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_VIEW_EVENT_DISPATCHER_H
#define ELG_VIEW_EVENT_DISPATCHER_H

#include "view/element.h"
#include "view/focus.h"

#include <elg/events.h>

#include <optional>
#include <vector>

namespace elg::detail {

// Routes one window's pointer input into its element tree (design.md §10):
// - down/up go to the handlers under the pointer, innermost first, until one consumes it;
//   consuming a down captures the pointer until the up;
// - hover enter/leave go to every handler under the pointer, after each one's hover delay
//   (tick() delivers them when due).
// - keys go to the focused element's handler, then bubble out through its ancestors until
//   consumed; an unconsumed Tab / Shift+Tab moves focus through the focusable elements in tree
//   order; pressing the pointer focuses the innermost focusable element under it (or clears focus).
// Each method returns true when the window needs a new frame.
class EventDispatcher {
public:
    EventDispatcher(Element& root, FocusManager& focus) : root_(&root), focus_(&focus) {}

    bool pointer_moved(Point location);
    bool pointer_button(PointerEvent::Kind kind, Point location, MouseButton button, int click_count,
                        KeyModifiers modifiers);
    bool pointer_left();
    // Whether the pointer is over this window, and its shape there: the innermost style under it
    // (or along the view a press is captured by), else the arrow.
    bool pointer_inside() const noexcept { return pointer_inside_; }
    PointerStyle pointer_style();
    bool key(const KeyEvent& event);
    // Menus: unhandled Up/Down move focus like Shift+Tab/Tab, Home/End to the first/last item.
    void set_menu_navigation(bool enabled) noexcept { menu_navigation_ = enabled; }
    // Focuses an element (no ring), e.g. a menu item under the pointer.
    bool focus(Element* element) { return focus_->focus(element, false); }
    bool text_input(const TextInputEvent& event);
    bool scroll(ScrollEvent event);
    // Trackpad gestures. MagnifyEvent::scale and RotateEvent::degrees are filled in here, from the
    // deltas since `began`.
    bool magnify(MagnifyEvent event);
    bool rotate(RotateEvent event);
    bool smart_magnify(Point location);
    // Whether pressing at `location` should move the window: the innermost handler there is a
    // .window_drag_area() (controls inside one take their clicks).
    bool drags_window_at(Point location);
    // Re-evaluates hover at the last pointer position (content may have moved under it, e.g.
    // scrolled).
    bool refresh_hover();
    // Call after If/ForEach removed elements: forgets elements that no longer exist (without
    // touching them) and re-evaluates hover at the last pointer position.
    bool revalidate();
    // Delivers hover changes whose delay has passed.
    bool tick();
    // When the next pending hover change is due (steady clock seconds), if any.
    std::optional<double> next_deadline() const;

private:
    // A handler-bearing element the pointer is over, or that still has to hear that it left.
    struct Hover {
        Element* element;
        bool over;      // the pointer is over it now
        bool reported;  // what its handler was last told
        double due;     // when to tell it `over`; < 0 when nothing is pending
    };

    std::vector<Element*> path_at(Point location);
    bool update_hover(const std::vector<Element*>& path);
    bool bubble(const std::vector<Element*>& path, const PointerEvent& event);
    bool move_focus(bool forward);
    bool focus_edge(bool first);

    Element* root_;
    FocusManager* focus_;
    bool menu_navigation_ = false;
    std::vector<Hover> hovers_; // outermost first
    Element* captured_ = nullptr;
    Point last_location_;
    bool pointer_inside_ = false;
    // Wheel latching: a scroll gesture stays with the element that took its first event (a page
    // scrolled past a horizontal shelf keeps scrolling) until the wheel pauses or the pointer
    // moves. Trackpad gestures with phases stay until the next one begins, momentum included.
    Element* wheel_latched_ = nullptr;
    bool wheel_phased_ = false; // the latched gesture has phases
    double last_wheel_ = 0;     // when the last wheel event came
    Point last_wheel_location_; // where
    // Pinch and rotate gestures go to whoever took their `began`.
    Element* magnify_latched_ = nullptr;
    Element* rotate_latched_ = nullptr;
    float magnify_scale_ = 1;
    float rotate_degrees_ = 0;
};

} // namespace elg::detail

#endif // ELG_VIEW_EVENT_DISPATCHER_H
