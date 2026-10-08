// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef SANDBOX_PALETTE_H
#define SANDBOX_PALETTE_H

#include <elg/color.h>
#include <elg/theme.h>

// Sandbox colors: accents used as-is, plus a light/dark theme for everything semantic
// (backgrounds, surfaces, text), read through ThemeColor tokens.
namespace palette {

inline constexpr elg::Color blue = elg::Color::hex(0x8AADF4);
inline constexpr elg::Color mauve = elg::Color::hex(0xC6A0F6);
inline constexpr elg::Color green = elg::Color::hex(0xA6DA95);
inline constexpr elg::Color peach = elg::Color::hex(0xF5A97F);
inline constexpr elg::Color sky = elg::Color::hex(0x7DC4E4);
inline constexpr elg::Color red = elg::Color::hex(0xED8796);

inline constexpr elg::Theme theme{
    .light = {
        .background = elg::Color::hex(0xEFF1F5),
        .surface = elg::Color::hex(0xFFFFFF),
        .surface_raised = elg::Color::hex(0xE6E9EF),
        .outline = elg::Color::hex(0xCCD0DA),
        .primary = elg::Color::hex(0x4C4F69),
        .secondary = elg::Color::hex(0x6C6F85),
        .tertiary = elg::Color::hex(0x9CA0B0),
        .accent = elg::Color::hex(0x1E66F5),
        .on_accent = elg::Color::white,
        .danger = elg::Color::hex(0xD20F39),
        .success = elg::Color::hex(0x40A02B),
        .warning = elg::Color::hex(0xDF8E1D),
    },
    .dark = {
        .background = elg::Color::hex(0x1E2030),
        .surface = elg::Color::hex(0x363A4F),
        .surface_raised = elg::Color::hex(0x494D64),
        .outline = elg::Color::hex(0x5B6078),
        .primary = elg::Color::hex(0xCAD3F5),
        .secondary = elg::Color::hex(0x939AB7),
        .tertiary = elg::Color::hex(0x6E738D),
        .accent = elg::Color::hex(0x8AADF4),
        .on_accent = elg::Color::hex(0x1E2030),
        .danger = elg::Color::hex(0xED8796),
        .success = elg::Color::hex(0xA6DA95),
        .warning = elg::Color::hex(0xEED49F),
    },
};

} // namespace palette

#endif // SANDBOX_PALETTE_H
