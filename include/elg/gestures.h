// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_GESTURES_H
#define ELG_GESTURES_H

// Gestures for View::recognize(): drags, long presses, and trackpad pinches, rotations and smart
// zoom. Each one is built up with the callbacks it needs:
//
//   .recognize(DragGesture({.minimum_distance = 8})
//                  .on_began([=] { start = position.peek(); })
//                  .on_changed([=](const DragEvent& e) { position = start() + e.translation; }))
//
// A pointer gesture watches presses on its view (and inside it) without taking them. Once it
// recognizes (a drag moves far enough, a long press is held long enough) it takes the press over:
// whatever had it, such as a button or an on_click, is cancelled and doesn't fire. The innermost
// gesture that recognizes wins. Controls that track the pointer themselves (sliders, text
// selection) keep their presses from gestures around them.

#include <elg/events.h>
#include <elg/geometry.h>

#include <functional>
#include <utility>

namespace elg {

namespace detail {
struct GestureAccess;
}

// A drag in progress. Locations are in the view's own coordinates; translation is measured in the
// window, so a view that moves with the drag doesn't feed back into it.
struct DragEvent {
    Point location;       // where the pointer is now
    Point start_location; // where the press went down
    Point translation;    // how far the pointer has moved since the press
    Point velocity;       // points per second, over the last moments of the drag
    MouseButton button = MouseButton::left;
    KeyModifiers modifiers = KeyModifiers::none; // held when the press went down
};

struct DragOptions {
    // How far the pointer moves (in points) before the drag begins. 0 begins it on the press.
    float minimum_distance = 4;
    MouseButton button = MouseButton::left;
};

class DragGesture {
public:
    explicit DragGesture(DragOptions options = {}) : options_(options) {}

    // The drag recognized: the pointer moved minimum_distance from the press.
    DragGesture on_began(std::function<void(const DragEvent&)> action) const {
        return with(&DragGesture::began_, std::move(action));
    }
    DragGesture on_began(std::function<void()> action) const {
        return on_began([action = std::move(action)](const DragEvent&) { action(); });
    }
    // The pointer moved (also right after on_began).
    DragGesture on_changed(std::function<void(const DragEvent&)> action) const {
        return with(&DragGesture::changed_, std::move(action));
    }
    // The button was released.
    DragGesture on_ended(std::function<void(const DragEvent&)> action) const {
        return with(&DragGesture::ended_, std::move(action));
    }
    // The pointer was lost (e.g. the window closed) before the release.
    DragGesture on_cancelled(std::function<void()> action) const {
        return with(&DragGesture::cancelled_, std::move(action));
    }

private:
    friend struct detail::GestureAccess;
    template <class F>
    DragGesture with(F DragGesture::*member, F action) const {
        DragGesture copy = *this;
        copy.*member = std::move(action);
        return copy;
    }

    DragOptions options_;
    std::function<void(const DragEvent&)> began_;
    std::function<void(const DragEvent&)> changed_;
    std::function<void(const DragEvent&)> ended_;
    std::function<void()> cancelled_;
};

struct LongPressOptions {
    double duration = 0.5;        // seconds held
    float maximum_distance = 10;  // points the pointer may wander before the press stops counting
    MouseButton button = MouseButton::left;
    // true when a press that could become the long press goes down, false when it lifts, moves
    // away or is taken by something else: for a "being held" highlight.
    std::function<void(bool)> pressing;
};

// Runs `action` once the press has been held for the duration. The click it would have been then
// doesn't happen.
class LongPressGesture {
public:
    explicit LongPressGesture(std::function<void()> action, LongPressOptions options = {})
        : action_(std::move(action)), options_(std::move(options)) {}

private:
    friend struct detail::GestureAccess;
    std::function<void()> action_;
    LongPressOptions options_;
};

// Phased trackpad gestures: pinching (MagnifyEvent) and turning two fingers (RotateEvent). The
// innermost one under the pointer that takes the start gets the rest.
template <class Event>
class PhasedGesture {
public:
    PhasedGesture on_began(std::function<void(const Event&)> action) const {
        return with(&PhasedGesture::began_, std::move(action));
    }
    PhasedGesture on_began(std::function<void()> action) const {
        return on_began([action = std::move(action)](const Event&) { action(); });
    }
    PhasedGesture on_changed(std::function<void(const Event&)> action) const {
        return with(&PhasedGesture::changed_, std::move(action));
    }
    PhasedGesture on_ended(std::function<void(const Event&)> action) const {
        return with(&PhasedGesture::ended_, std::move(action));
    }
    // The system took the gesture over.
    PhasedGesture on_cancelled(std::function<void()> action) const {
        return with(&PhasedGesture::cancelled_, std::move(action));
    }

private:
    friend struct detail::GestureAccess;
    template <class F>
    PhasedGesture with(F PhasedGesture::*member, F action) const {
        PhasedGesture copy = *this;
        copy.*member = std::move(action);
        return copy;
    }

    std::function<void(const Event&)> began_;
    std::function<void(const Event&)> changed_;
    std::function<void(const Event&)> ended_;
    std::function<void()> cancelled_;
};

// Pinching: scale is since the gesture began, delta since the last event.
using MagnifyGesture = PhasedGesture<MagnifyEvent>;
// Turning two fingers: degrees clockwise since the gesture began.
using RotateGesture = PhasedGesture<RotateEvent>;

// A two-finger double tap on a trackpad (usually: zoom to fit). `action` gets where it was, in
// window coordinates.
class TwoFingerDoubleTapGesture {
public:
    explicit TwoFingerDoubleTapGesture(std::function<void(Point)> action) : action_(std::move(action)) {}

private:
    friend struct detail::GestureAccess;
    std::function<void(Point)> action_;
};

} // namespace elg

#endif // ELG_GESTURES_H
