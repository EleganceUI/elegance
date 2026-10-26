// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_PLATFORM_H
#define ELG_PLATFORM_H

// The platform the app runs on, for UI that follows its conventions:
//
//   .padding(EdgeInsets{.trailing = current_platform() == Platform::windows ? 138.f : 0.f})
//
// For room around a window's own controls, prefer WindowHandle::system_controls_frame()
// (elg/window.h): it also follows fullscreen and the controls' position.

namespace elg {

enum class Platform {
    windows,
    macos,
    linux,
};

constexpr Platform current_platform() noexcept {
#if defined(_WIN32)
    return Platform::windows;
#elif defined(__APPLE__)
    return Platform::macos;
#else
    return Platform::linux;
#endif
}

} // namespace elg

#endif // ELG_PLATFORM_H
