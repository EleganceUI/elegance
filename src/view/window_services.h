// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_VIEW_WINDOW_SERVICES_H
#define ELG_VIEW_WINDOW_SERVICES_H

#include <elg/environment.h>
#include <elg/geometry.h>
#include <elg/view.h>

#include <functional>
#include <string>

namespace elg::detail {

enum class CursorShape { arrow, text };

// A popup window to open (design.md §13.1), anchored to a rect in the opener's window.
struct PopupRequest {
    enum class Kind { menu, tooltip };
    enum class Placement {
        at_point, // the panel's corner at anchor's origin (context menus)
        below,    // under the anchor, left edges aligned (dropdowns, tooltips)
        beside,   // to the right of the anchor, tops aligned (submenus)
    };

    Kind kind = Kind::menu;
    Placement placement = Placement::below;
    Rect anchor;                     // the opener window's logical points
    std::function<View()> content;   // built when the popup mounts, in its own scope
    EnvironmentValues environment;   // the opener's: theme, fonts, tint carry over
    int level = 0;                   // menus: 0 for the first, +1 per submenu
};

// What elements need from the window they're in, beyond drawing: text input (IME), the
// clipboard, the pointer's shape, and timed redraws. Implemented by the app per window.
class WindowServices {
public:
    virtual ~WindowServices() = default;

    // Text input on/off (on while a text field has focus): typed text and IME composition
    // arrive as TextInputEvents.
    virtual void start_text_input() = 0;
    virtual void stop_text_input() = 0;
    // Where text is being edited (window points), so the IME places its candidate list there.
    virtual void set_text_input_area(Rect area, float cursor) = 0;

    virtual std::string clipboard_text() = 0;
    virtual void set_clipboard_text(const std::string& text) = 0;

    virtual void set_cursor(CursorShape shape) = 0;

    // Requests a new frame at `seconds` (steady clock), e.g. for a blinking caret.
    virtual void wake_at(double seconds) = 0;

    // Popups. Requests take effect after the current event (never while it's being handled).
    // Opening a menu at level n closes menus at level n and deeper first.
    virtual void open_popup(PopupRequest request) = 0;
    // Closes menus at `level` and deeper (0: all menus).
    virtual void close_menus(int level = 0) = 0;
    // Closes every menu, then runs `action` (menu item actions).
    virtual void close_menus_then(std::function<void()> action) = 0;
    virtual void close_tooltip() = 0;
    // For a menu popup window: its level; -1 for other windows.
    virtual int menu_level() const = 0;
};

} // namespace elg::detail

#endif // ELG_VIEW_WINDOW_SERVICES_H
