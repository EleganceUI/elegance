// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_PLATFORM_GENERIC_DESKTOP_SDL_H
#define ELG_PLATFORM_GENERIC_DESKTOP_SDL_H

namespace elg::detail::platform {

// Takes every tray icon out of the system tray, before SDL shuts down at the end of a run. The
// TrayIcons the app still holds then do nothing, and call nothing when they're destroyed.
void remove_tray_icons();

} // namespace elg::detail::platform

#endif // ELG_PLATFORM_GENERIC_DESKTOP_SDL_H
