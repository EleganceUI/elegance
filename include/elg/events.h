// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_EVENTS_H
#define ELG_EVENTS_H

// Pointer events and the handler interface event modifiers provide (design.md §10).

#include <elg/export.h>
#include <elg/geometry.h>

#include <optional>

namespace elg {

// How long the pointer must rest on a view before it counts as hovered (`enter`), and how long
// after it leaves before it stops counting (`exit`), in seconds. Moving across a view faster than
// `enter` doesn't hover it; coming back within `exit` cancels the leave.
// Set for a subtree with .hover_delay({...}), or per handler with .on_hover(action, {...}).
struct HoverDelay {
    double enter = 0.1;
    double exit = 0.0;
};

enum class MouseButton { left, middle, right, other };

struct PointerEvent {
    enum class Kind {
        down,
        up,
        move,   // only sent to the handler that captured the pointer
        cancel, // the pointer was lost (e.g. the window closed); reset any pressed state
    };

    Kind kind = Kind::move;
    Point location; // window coordinates, in points
    MouseButton button = MouseButton::left;
    int click_count = 1; // 2 for the second click of a double click, ...
};

struct EventResult {
    // Stops the event from reaching outer handlers. Consuming a `down` captures the pointer:
    // this handler then receives the moves and the `up`, wherever the pointer goes.
    bool consumed = false;
    // The handler changed how something looks; the window needs a new frame.
    bool redraw = false;
};

// Receives events for one mounted view. Created by an EventModifier when the view mounts.
class ELG_API EventHandler {
public:
    virtual ~EventHandler();

    // `frame` is the view's current frame in window coordinates. Events arrive innermost view
    // first, then bubble outwards until consumed.
    virtual EventResult pointer(const PointerEvent& event, Rect frame);
    // The pointer entered (true) or left (false) the view, after the hover delay.
    virtual EventResult hover(bool inside);
    // This handler's own hover delay; nullopt uses the environment's (.hover_delay()).
    virtual std::optional<HoverDelay> hover_delay() const { return std::nullopt; }
};

} // namespace elg

#endif // ELG_EVENTS_H
