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
// selection) keep their presses from gestures around them. GestureOptions change that for a
// gesture: it can go first, or run alongside the others.

#include <elg/error.h>
#include <elg/events.h>
#include <elg/geometry.h>

#include <functional>
#include <utility>

namespace elg {

namespace detail {
struct GestureAccess;
}

/// A drag in progress. Locations are in the view's own coordinates; translation is measured in the
/// window, so a view that moves with the drag doesn't feed back into it.
struct DragEvent {
    Point location;       ///< where the pointer is now
    Point start_location; ///< where the press went down
    Point translation;    ///< how far the pointer has moved since the press
    Point velocity;       ///< points per second, over the last moments of the drag
    MouseButton button = MouseButton::left;      ///< the button that's dragging
    KeyModifiers modifiers = KeyModifiers::none; ///< held when the press went down
};

/// When a DragGesture begins.
struct DragOptions {
    /// How far the pointer moves (in points) before the drag begins. 0 begins it on the press.
    float minimum_distance = 4;
    MouseButton button = MouseButton::left; ///< the button that drags
    /// Hold, then drag (reordering by touch): seconds the press must be held, within
    /// hold_tolerance points, before the drag can begin. It then begins at once (on_began with no
    /// translation, for a "lifted" look) and follows the pointer. A press that moves sooner is
    /// left to the rest (scrolling, clicks). 0: no hold.
    double hold = 0;
    float hold_tolerance = 10; ///< how far (in points) the pointer may move during the `hold`
};

/// A drag on a view, for View::recognize(). Set the callbacks it needs; each setter returns a
/// copy with that callback set (replacing an earlier one).
///
/// @code
/// .recognize(DragGesture({.minimum_distance = 8})
///                .on_began([=] { start = position.peek(); })
///                .on_changed([=](const DragEvent& e) { position = start() + e.translation; }))
/// @endcode
class DragGesture {
public:
    /// A drag with no callbacks yet.
    ///
    /// @param options When it begins: how far the pointer must move, which button, a hold first.
    explicit DragGesture(DragOptions options = {}) : options_(options) {}

    /// The drag recognized: the pointer moved minimum_distance from the press.
    ///
    /// @param action Runs once, with the event where the drag began. May be empty.
    /// @return A copy of this gesture with `action` set.
    [[nodiscard]] DragGesture on_began(std::function<void(const DragEvent&)> action) const {
        return with(&DragGesture::began_, std::move(action));
    }
    /// Like on_began(std::function<void(const DragEvent&)>), for an action that doesn't need
    /// the event.
    ///
    /// @param action Runs once, when the drag begins. May be empty.
    /// @return A copy of this gesture with `action` set.
    [[nodiscard]] DragGesture on_began(std::function<void()> action) const {
        if (!action) {
            return on_began(std::function<void(const DragEvent&)>());
        }
        return on_began([action = std::move(action)](const DragEvent&) { action(); });
    }
    /// The pointer moved (also right after on_began).
    ///
    /// @param action Runs with each move of the drag. May be empty.
    /// @return A copy of this gesture with `action` set.
    [[nodiscard]] DragGesture on_changed(std::function<void(const DragEvent&)> action) const {
        return with(&DragGesture::changed_, std::move(action));
    }
    /// The button was released.
    ///
    /// @param action Runs once, with the event where the drag ended. May be empty.
    /// @return A copy of this gesture with `action` set.
    [[nodiscard]] DragGesture on_ended(std::function<void(const DragEvent&)> action) const {
        return with(&DragGesture::ended_, std::move(action));
    }
    /// The pointer was lost (e.g. the window closed) before the release.
    ///
    /// @param action Runs once, instead of the on_ended action. May be empty.
    /// @return A copy of this gesture with `action` set.
    [[nodiscard]] DragGesture on_cancelled(std::function<void()> action) const {
        return with(&DragGesture::cancelled_, std::move(action));
    }

private:
    friend struct detail::GestureAccess;
    template <class F>
    [[nodiscard]] DragGesture with(F DragGesture::*member, F action) const {
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

/// How long a LongPressGesture is held, and what may happen meanwhile.
struct LongPressOptions {
    double duration = 0.5;        ///< seconds held
    float maximum_distance = 10;  ///< points the pointer may wander before the press stops counting
    MouseButton button = MouseButton::left; ///< the button to hold
    /// true when a press that could become the long press goes down, false when it lifts, moves
    /// away or is taken by something else: for a "being held" highlight.
    std::function<void(bool)> pressing;
};

/// A long press on a view, for View::recognize(). Once it fires, the rest of the press is its own.
class LongPressGesture {
public:
    /// Runs `action` once the press has been held for the duration. The click it would have been
    /// then doesn't happen.
    ///
    /// @param action Runs once per long press, when it has been held long enough (before the
    ///   release). May be empty.
    /// @param options How long, how still, which button, and a `pressing` callback.
    explicit LongPressGesture(std::function<void()> action, LongPressOptions options = {})
        : action_(std::move(action)), options_(std::move(options)) {}

private:
    friend struct detail::GestureAccess;
    std::function<void()> action_;
    LongPressOptions options_;
};

/// How a pointer gesture (drag, long press) competes with others for a press.
enum class GesturePriority {
    normal, ///< the innermost gesture that recognizes the press takes it
    /// Offered the press before gestures inside its view, and may take it from controls that keep
    /// their presses (a slider): a row's swipe over the controls in it.
    high,
};

/// How a pointer gesture shares presses with others (View::recognize()).
struct GestureOptions {
    GesturePriority priority = GesturePriority::normal; ///< who gets the press first
    /// Recognizes alongside whatever has the press (a gesture, a control), without taking it: the
    /// others aren't cancelled, and it isn't cancelled when they recognize. Priority doesn't apply.
    bool simultaneous = false;
};

/// Phased trackpad gestures: pinching (MagnifyEvent) and turning two fingers (RotateEvent). The
/// innermost one under the pointer that takes the start gets the rest. Each setter returns a copy
/// with that callback set (replacing an earlier one).
///
/// @tparam Event MagnifyEvent or RotateEvent; use MagnifyGesture and RotateGesture.
template <class Event>
class PhasedGesture {
public:
    /// The fingers touched down: the gesture began.
    ///
    /// @param action Runs once, with the first event. May be empty.
    /// @return A copy of this gesture with `action` set.
    [[nodiscard]] PhasedGesture on_began(std::function<void(const Event&)> action) const {
        return with(&PhasedGesture::began_, std::move(action));
    }
    /// Like on_began(std::function<void(const Event&)>), for an action that doesn't need the
    /// event.
    ///
    /// @param action Runs once, when the gesture begins. May be empty.
    /// @return A copy of this gesture with `action` set.
    [[nodiscard]] PhasedGesture on_began(std::function<void()> action) const {
        if (!action) {
            return on_began(std::function<void(const Event&)>());
        }
        return on_began([action = std::move(action)](const Event&) { action(); });
    }
    /// The fingers moved. Also gets events that aren't part of a tracked gesture
    /// (GesturePhase::none).
    ///
    /// @param action Runs with each event. May be empty.
    /// @return A copy of this gesture with `action` set.
    [[nodiscard]] PhasedGesture on_changed(std::function<void(const Event&)> action) const {
        return with(&PhasedGesture::changed_, std::move(action));
    }
    /// The fingers lifted.
    ///
    /// @param action Runs once, with the last event. May be empty.
    /// @return A copy of this gesture with `action` set.
    [[nodiscard]] PhasedGesture on_ended(std::function<void(const Event&)> action) const {
        return with(&PhasedGesture::ended_, std::move(action));
    }
    /// The system took the gesture over.
    ///
    /// @param action Runs once, instead of the on_ended action. May be empty.
    /// @return A copy of this gesture with `action` set.
    [[nodiscard]] PhasedGesture on_cancelled(std::function<void()> action) const {
        return with(&PhasedGesture::cancelled_, std::move(action));
    }

private:
    friend struct detail::GestureAccess;
    template <class F>
    [[nodiscard]] PhasedGesture with(F PhasedGesture::*member, F action) const {
        PhasedGesture copy = *this;
        copy.*member = std::move(action);
        return copy;
    }

    std::function<void(const Event&)> began_;
    std::function<void(const Event&)> changed_;
    std::function<void(const Event&)> ended_;
    std::function<void()> cancelled_;
};

/// Pinching: scale is since the gesture began, delta since the last event.
using MagnifyGesture = PhasedGesture<MagnifyEvent>;
/// Turning two fingers: degrees clockwise since the gesture began.
using RotateGesture = PhasedGesture<RotateEvent>;

/// A two-finger double tap on a trackpad (usually: zoom to fit), for View::recognize().
class TwoFingerDoubleTapGesture {
public:
    /// A gesture that runs `action` on each two-finger double tap over the view.
    ///
    /// @param action Gets where the tap was, in window coordinates. May be empty.
    explicit TwoFingerDoubleTapGesture(std::function<void(Point)> action) : action_(std::move(action)) {}

private:
    friend struct detail::GestureAccess;
    std::function<void(Point)> action_;
};

} // namespace elg

#endif // ELG_GESTURES_H
