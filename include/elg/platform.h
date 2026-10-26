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

// GCC and Clang predefine `linux` as 1 on Linux in GNU modes (-std=gnu++23, CMake's default for
// targets without CXX_EXTENSIONS OFF), which would turn Platform::linux into Platform::1. Hide it
// here and restore it after, so including this header works in either mode. Code naming
// Platform::linux in a GNU mode needs `#undef linux` first (no code should use the macro).
#pragma push_macro("linux")
#undef linux

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

#pragma pop_macro("linux")

#endif // ELG_PLATFORM_H
