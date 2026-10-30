// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_PLATFORM_MACOS_TITLE_BAR_H
#define ELG_PLATFORM_MACOS_TITLE_BAR_H

// TitleBar::integrated on macOS: the content view fills the window under a transparent title
// bar, and the native traffic lights stay, optionally moved.

#include <elg/geometry.h>

#include <optional>

namespace elg::detail::platform::macos {

// Integrates `ns_window`'s title bar (an NSWindow*), keeping its content size. Returns what keeps
// the traffic lights in place as the window changes; pass it to the functions below.
void* integrate_title_bar(void* ns_window, std::optional<Point> traffic_lights);
// Integrates the title bar again: SDL restores the plain one when the window leaves fullscreen.
void refresh_title_bar(void* title_bar);
// The traffic lights' frame, in points from the content's top-left corner; empty in fullscreen.
Rect system_controls_frame(void* title_bar);
void release_title_bar(void* title_bar);

} // namespace elg::detail::platform::macos

#endif // ELG_PLATFORM_MACOS_TITLE_BAR_H
