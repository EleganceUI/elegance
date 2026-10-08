// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef SANDBOX_PALETTE_H
#define SANDBOX_PALETTE_H

#include <elg/color.h>

// Sandbox colors. A stand-in until themes and environment values land (M3).
namespace palette {

inline constexpr elg::Color base = elg::Color::hex(0x1E2030);
inline constexpr elg::Color mantle = elg::Color::hex(0x24273A);
inline constexpr elg::Color surface = elg::Color::hex(0x363A4F);
inline constexpr elg::Color outline = elg::Color::hex(0x5B6078);
inline constexpr elg::Color muted = elg::Color::hex(0x494D64);
inline constexpr elg::Color blue = elg::Color::hex(0x8AADF4);
inline constexpr elg::Color mauve = elg::Color::hex(0xC6A0F6);
inline constexpr elg::Color green = elg::Color::hex(0xA6DA95);
inline constexpr elg::Color peach = elg::Color::hex(0xF5A97F);
inline constexpr elg::Color sky = elg::Color::hex(0x7DC4E4);
inline constexpr elg::Color red = elg::Color::hex(0xED8796);
inline constexpr elg::Color text = elg::Color::hex(0xCAD3F5);
inline constexpr elg::Color subtext = elg::Color::hex(0x939AB7);

} // namespace palette

#endif // SANDBOX_PALETTE_H
