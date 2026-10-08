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

// How long the pointer must rest on a view before it counts as hovered (`enter`), and how long
// after it leaves before it stops counting (`exit`), in seconds. Moving across a view faster than
// `enter` doesn't hover it; coming back within `exit` cancels the leave.
// Set for a subtree with .hover_delay({...}), or per handler with .on_hover(action, {...}).
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

// A mouse wheel or trackpad scroll. Deltas are in points, positive toward the end of the content
// (wheel down: positive dy).
struct ScrollEvent {
    Point location;
    float dx = 0;
    float dy = 0;
    bool precise = false; // trackpad-style fine deltas (apply directly) rather than wheel notches
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
    };

    Kind kind = Kind::move;
    Point location; // window coordinates, in points
    MouseButton button = MouseButton::left;
    int click_count = 1; // 2 for the second click of a double click, ...
    KeyModifiers modifiers = KeyModifiers::none; // held when the button went down / up
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

    // Wheel and trackpad scrolling: goes to the handlers under the pointer, innermost first,
    // until one consumes it.
    virtual EventResult scroll(const ScrollEvent& event);

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
};

} // namespace elg

#endif // ELG_EVENTS_H
