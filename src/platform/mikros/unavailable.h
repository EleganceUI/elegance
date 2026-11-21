// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_PLATFORM_MIKROS_UNAVAILABLE_H
#define ELG_PLATFORM_MIKROS_UNAVAILABLE_H

// What Mikros doesn't offer (networking, audio, video, cameras, notifications, tray icons): the
// message that using one of them fails with.

#include <elg/error.h>

#include <format>
#include <string>
#include <string_view>

namespace elg::detail::platform::mikros {

// The message, for what's shown without throwing (a player's error()).
inline std::string unavailable_message(std::string_view what) {
    return std::format("Mikros doesn't offer {}", what);
}

// Throws elg::PlatformError with it.
[[noreturn]] inline void unavailable(std::string_view what) {
    throw PlatformError(unavailable_message(what));
}

} // namespace elg::detail::platform::mikros

#endif // ELG_PLATFORM_MIKROS_UNAVAILABLE_H
