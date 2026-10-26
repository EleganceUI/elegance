// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_PLATFORM_INPUT_APPLE_H
#define ELG_PLATFORM_INPUT_APPLE_H

// Trackpad input on macOS, taken from AppKit before SDL sees it: SDL drops the gesture phases
// (scrolls, momentum) and doesn't report rotation or smart magnify.

#include <elg/events.h>

#include <functional>

namespace elg::detail::platform::apple {

struct NativeInput {
    enum class Kind { scroll, magnify, rotate, smart_magnify };

    Kind kind = Kind::scroll;
    void* ns_window = nullptr; // the NSWindow it happened in
    // Where, in points from the content's top-left corner.
    float x = 0;
    float y = 0;
    // Scrolls: points (precise) or lines (a mouse wheel), positive toward the end of the content.
    float dx = 0;
    float dy = 0;
    bool precise = false;
    GesturePhase phase = GesturePhase::none;
    bool momentum = false;
    // Magnify: the change in scale (0.1 = 10% larger). Rotate: degrees, clockwise.
    float amount = 0;
    double time = 0; // seconds since startup
};

// Calls `handle` on the main thread for each trackpad event; it returns whether it took the
// event (SDL then never sees it). Returns the monitor, for remove_input_monitor().
void* install_input_monitor(std::function<bool(const NativeInput&)> handle);
void remove_input_monitor(void* monitor);

} // namespace elg::detail::platform::apple

#endif // ELG_PLATFORM_INPUT_APPLE_H
