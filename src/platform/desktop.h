// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_PLATFORM_DESKTOP_H
#define ELG_PLATFORM_DESKTOP_H

#include <elg/desktop.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace elg::detail::platform {

// Takes every tray icon out of the system tray, before SDL shuts down at the end of a run. The
// TrayIcons the app still holds then do nothing, and call nothing when they're destroyed.
void remove_tray_icons();

// Throws elg::UsageError, naming `what`, when no App runs: what the desktop functions check first.
void require_app(const char* what);

// A tray icon's picture: RGBA8, straight alpha, rows top to bottom.
struct TrayIconPicture {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::shared_ptr<const std::vector<std::uint8_t>> rgba;
};
// Puts an icon with `options` in the system tray, drawn as `picture`, or as a plain blue dot
// without one: TrayIcon's constructor (view/tray_icon.cpp), which decodes the picture.
std::shared_ptr<TrayIcon::State> create_tray_icon(TrayIconOptions options, std::optional<TrayIconPicture> picture);

} // namespace elg::detail::platform

#endif // ELG_PLATFORM_DESKTOP_H
