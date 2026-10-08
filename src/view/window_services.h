// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_VIEW_WINDOW_SERVICES_H
#define ELG_VIEW_WINDOW_SERVICES_H

#include <elg/geometry.h>

#include <string>

namespace elg::detail {

enum class CursorShape { arrow, text };

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
};

} // namespace elg::detail

#endif // ELG_VIEW_WINDOW_SERVICES_H
