// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_VIEW_GESTURES_H
#define ELG_VIEW_GESTURES_H

#include <elg/events.h>

#include <optional>

namespace elg::detail {

// A pointer gesture (drag, long press): watches presses on its view without taking them, until it
// recognizes one and takes it over (EventDispatcher). From then on the press's events come to
// pointer(), as to any handler that captured the pointer.
class GestureRecognizer : public EventHandler {
public:
    // The press's down, moves and up, in the coordinates pointer() gets. true takes the press over.
    virtual bool observe(const PointerEvent& event, Rect frame) = 0;
    // When it wants expire() (steady clock seconds, like EventDispatcher::next_deadline()).
    virtual std::optional<double> deadline() const { return std::nullopt; }
    // The deadline passed: true takes the press over.
    virtual bool expire() { return false; }
    // The press ended without this taking it, or something else took it.
    virtual void reset() = 0;
};

} // namespace elg::detail

#endif // ELG_VIEW_GESTURES_H
