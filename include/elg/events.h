// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_EVENTS_H
#define ELG_EVENTS_H

// Pointer events and the handler interface event modifiers provide (design.md §10).

#include <elg/export.h>
#include <elg/geometry.h>

#include <cstddef>
#include <optional>
#include <string>

namespace elg {

// Hover intent (on_hover_change, submenus): how long the pointer must rest on a view before it
// counts as hovered (`enter`), and how long after it leaves before it stops counting (`exit`), in
// seconds. Moving across a view faster than `enter` doesn't hover it; coming back within `exit`
// cancels the leave. Set with .hover_delay({...}) for everything inside, one handler included:
// .on_hover_change(action).hover_delay({...}). Pointer enter/leave (and controls' highlights) don't wait.
struct HoverDelay {
    double enter = 0.1;
    double exit = 0.0;
};

enum class MouseButton { left, middle, right, other };

// Keys by meaning (the layout-mapped key, not the physical position).
enum class Key {
    unknown,
    a, b, c, d, e, f, g, h, i, j, k, l, m, n, o, p, q, r, s, t, u, v, w, x, y, z,
    digit0, digit1, digit2, digit3, digit4, digit5, digit6, digit7, digit8, digit9,
    enter, escape, backspace, tab, space, forward_delete, insert,
    home, end, page_up, page_down, left, right, up, down,
    f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12,
    menu, // the context-menu (application) key
};

enum class KeyModifiers : unsigned {
    none = 0,
    shift = 1,
    control = 2,
    alt = 4,
    super = 8, // Cmd on macOS, Windows key on Windows
};

constexpr KeyModifiers operator|(KeyModifiers a, KeyModifiers b) noexcept {
    return static_cast<KeyModifiers>(static_cast<unsigned>(a) | static_cast<unsigned>(b));
}

constexpr bool has(KeyModifiers set, KeyModifiers modifier) noexcept {
    return (static_cast<unsigned>(set) & static_cast<unsigned>(modifier)) != 0;
}

// The platform's shortcut modifier: Cmd on macOS, Ctrl elsewhere.
constexpr KeyModifiers primary_modifier() noexcept {
#if defined(__APPLE__)
    return KeyModifiers::super;
#else
    return KeyModifiers::control;
#endif
}

// The mouse pointer's shape over a view, like CSS's `cursor` (View::pointer_style()). The
// innermost view under the pointer with a style other than `automatic` decides; with none, it's
// the arrow. It follows the pointer and the views: one going away takes its style with it.
enum class PointerStyle {
    automatic, // no style of its own: the views around it decide
    arrow,     // CSS's default
    pointer,   // a hand, over links and other things to click
    text,      // the I-beam, over text to select or edit
    crosshair,
    move,
    wait,     // busy: nothing can be done until it's over
    progress, // busy, but it still takes input
    resize_horizontal,
    resize_vertical,
    resize_nwse, // corners: top-left to bottom-right
    resize_nesw, // corners: top-right to bottom-left
    not_allowed,
};

// Where a trackpad gesture is. Mouse wheels have none.
enum class GesturePhase {
    none,      // not part of a gesture (a mouse wheel, or a trackpad the platform doesn't follow)
    began,     // fingers touched down (scrolls: possibly before moving, with no delta)
    changed,
    ended,     // fingers lifted
    cancelled, // the system took the gesture over
};

// A mouse wheel or trackpad scroll. Deltas are in points, positive toward the end of the content
// (wheel down: positive dy).
struct ScrollEvent {
    // Where the pointer is: in window coordinates for an EventHandler, in the view's own for
    // View::on_scroll.
    Point location;
    Point window_location; // always in the window's coordinates
    float dx = 0;
    float dy = 0;
    bool precise = false; // trackpad-style fine deltas (apply directly) rather than wheel notches
    KeyModifiers modifiers = KeyModifiers::none; // held while scrolling (Ctrl+wheel: zoom)
    // Trackpads on macOS: the fingers' gesture (began ... ended), then, after a flick, the system's
    // momentum (momentum = true, began ... ended) as the scroll coasts to a stop.
    GesturePhase phase = GesturePhase::none;
    bool momentum = false;
    // When it happened, in seconds on the event source's clock (for speeds between events); 0 if
    // unknown.
    double time = 0;
};

// Pinching on a trackpad (or a touch screen).
struct MagnifyEvent {
    Point location;
    float scale = 1;     // since the gesture began: 2 = twice as big
    float delta = 1;     // since the last event, as a factor
    GesturePhase phase = GesturePhase::changed;
};

// Turning two fingers on a trackpad.
struct RotateEvent {
    Point location;
    float degrees = 0;   // since the gesture began, clockwise
    float delta = 0;     // since the last event
    GesturePhase phase = GesturePhase::changed;
};

// Text from the keyboard or an input method, for views that accept text (TextField).
struct TextInputEvent {
    enum class Kind {
        commit,      // insert `text`
        composition, // an IME's in-progress text, replacing the previous one ("" ends it)
    };

    Kind kind = Kind::commit;
    std::string text;
    std::size_t cursor = 0; // composition: the IME's caret, in bytes into `text`
};

struct KeyEvent {
    enum class Kind { down, up };

    Kind kind = Kind::down;
    Key key = Key::unknown;
    KeyModifiers modifiers = KeyModifiers::none;
    bool repeat = false; // auto-repeat while held
};

struct PointerEvent {
    enum class Kind {
        down,
        up,
        move,   // only sent to the handler that captured the pointer
        cancel, // the pointer was lost (e.g. the window closed); reset any pressed state
        // The pointer moved over the view while nothing has it captured (no button held on
        // something). Innermost first, until one consumes it: for highlights and cursors that
        // depend on where the pointer is.
        hover,
    };

    Kind kind = Kind::move;
    // In points: window coordinates for an EventHandler, the view's own for View::on_pointer.
    Point location;
    MouseButton button = MouseButton::left;
    int click_count = 1; // 2 for the second click of a double click, ...
    KeyModifiers modifiers = KeyModifiers::none; // held when the button went down / up
    Point window_location;                       // always in the window's coordinates
};

// A click on a view (View::on_click): pressed and released inside it.
struct ClickEvent {
    Point location;        // where it was released, in the view's own coordinates (0, 0: its top-leading corner)
    Point window_location; // the same, in the window's
    MouseButton button = MouseButton::left;
    KeyModifiers modifiers = KeyModifiers::none; // held when it was released
    int click_count = 1;                         // 2 for a double click, ...
};

// Which clicks View::on_click takes.
struct ClickOptions {
    MouseButton button = MouseButton::left;
    // 2: double clicks (the first click goes on to outer handlers), 3: triple clicks, ...
    int count = 1;
};

struct EventResult {
    // Stops the event from reaching outer handlers. Consuming a `down` captures the pointer:
    // this handler then receives the moves and the `up`, wherever the pointer goes.
    bool consumed = false;
    // The handler changed how something looks; the window needs a new frame.
    bool redraw = false;
    // With `consumed` on a down: gesture recognizers around the view (View::recognize) may take
    // the press over, and the handler then gets `cancel`. For presses that are clicks (buttons);
    // handlers that track the pointer themselves (sliders) leave it false.
    bool cancellable = false;
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
    // The pointer's shape at `location` (window points) over this view. Asked innermost first,
    // and of the view a press is captured by while it is; `automatic` leaves it to the views
    // around. For styles that depend on where the pointer is (a canvas's resize handles); a fixed
    // one is View::pointer_style().
    virtual PointerStyle pointer_style(Point location) const;

    // Wheel and trackpad scrolling: goes to the handlers under the pointer, innermost first,
    // until one consumes it. `frame` as for pointer().
    virtual EventResult scroll(const ScrollEvent& event, Rect frame);
    // Trackpad gestures: the handler under the pointer that consumes `began` gets the rest of the
    // gesture, innermost first. A two-finger double tap usually zooms to fit.
    virtual EventResult magnify(const MagnifyEvent& event);
    virtual EventResult rotate(const RotateEvent& event);
    virtual EventResult two_finger_double_tap(Point location);

    // Keyboard (design.md §10): key events go to the focused view's handler first, then bubble
    // to the handlers around it until consumed.
    virtual EventResult key(const KeyEvent& event);
    // Whether the view can take keyboard focus (Tab, clicks).
    virtual bool focusable() const { return false; }
    // The view gained (true) or lost (false) keyboard focus.
    virtual EventResult focus(bool focused);

    // Text input goes to the focused view's handler (then bubbles) while it accepts text.
    virtual bool accepts_text_input() const { return false; }
    virtual EventResult text_input(const TextInputEvent& event);
    // This handler's own hover delay; nullopt uses the environment's (.hover_delay()).
    virtual std::optional<HoverDelay> hover_delay() const { return std::nullopt; }
    // true: the view is hit anywhere in its frame, not only where its content draws (a window
    // drag area's gaps and Spacers still drag).
    virtual bool covers_frame() const { return false; }
};

} // namespace elg

#endif // ELG_EVENTS_H
