// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_THEME_H
#define ELG_THEME_H

// Themes (design.md §8): semantic color tokens with light and dark variants.

#include <elg/color.h>
#include <elg/export.h>

#include <optional>

namespace elg {

enum class ColorScheme { light, dark };

// Semantic colors, resolved against the environment's theme and color scheme where they're used:
//   Text("Details").foreground_style(ThemeColor::secondary)
//   RoundedRectangle(12).fill(ThemeColor::surface)
enum class ThemeColor {
    background,     // window background
    surface,        // cards, panels
    surface_raised, // controls and panels on surfaces
    outline,        // borders, separators
    primary,        // main text and icons
    secondary,      // supporting text
    tertiary,       // hints, disabled text
    accent,         // the tint: prominent buttons, selection
    on_accent,      // text and icons on the accent
    danger,
    success,
    warning,
};

struct ColorTokens {
    Color background;
    Color surface;
    Color surface_raised;
    Color outline;
    Color primary;
    Color secondary;
    Color tertiary;
    Color accent;
    Color on_accent;
    Color danger;
    Color success;
    Color warning;

    constexpr Color operator[](ThemeColor token) const noexcept {
        switch (token) {
        case ThemeColor::background: return background;
        case ThemeColor::surface: return surface;
        case ThemeColor::surface_raised: return surface_raised;
        case ThemeColor::outline: return outline;
        case ThemeColor::primary: return primary;
        case ThemeColor::secondary: return secondary;
        case ThemeColor::tertiary: return tertiary;
        case ThemeColor::accent: return accent;
        case ThemeColor::on_accent: return on_accent;
        case ThemeColor::danger: return danger;
        case ThemeColor::success: return success;
        case ThemeColor::warning: return warning;
        }
        return primary;
    }
};

struct Theme {
    ColorTokens light;
    ColorTokens dark;

    constexpr const ColorTokens& colors(ColorScheme scheme) const noexcept {
        return scheme == ColorScheme::dark ? dark : light;
    }

    // The default: neutral grays with a blue accent.
    static constexpr Theme standard() noexcept {
        return {
            .light = {
                .background = Color::hex(0xF5F5F7),
                .surface = Color::hex(0xFFFFFF),
                .surface_raised = Color::hex(0xECECEF),
                .outline = Color::hex(0xD1D1D6),
                .primary = Color::hex(0x1C1C1E),
                .secondary = Color::hex(0x6C6C70),
                .tertiary = Color::hex(0xAEAEB2),
                .accent = Color::hex(0x0A84FF),
                .on_accent = Color::white,
                .danger = Color::hex(0xFF3B30),
                .success = Color::hex(0x34C759),
                .warning = Color::hex(0xFF9F0A),
            },
            .dark = {
                .background = Color::hex(0x1C1C1E),
                .surface = Color::hex(0x2C2C2E),
                .surface_raised = Color::hex(0x3A3A3C),
                .outline = Color::hex(0x48484A),
                .primary = Color::hex(0xF2F2F7),
                .secondary = Color::hex(0xAEAEB2),
                .tertiary = Color::hex(0x636366),
                .accent = Color::hex(0x0A84FF),
                .on_accent = Color::white,
                .danger = Color::hex(0xFF453A),
                .success = Color::hex(0x30D158),
                .warning = Color::hex(0xFFD60A),
            },
        };
    }
};

// The accent color the person chose in the system's settings, or nullopt where there's none
// (or the platform doesn't say: Linux desktops through the settings portal, if at all).
// Reactive: reads update when it changes. UI thread, while an App runs.
ELG_API std::optional<Color> system_accent_color();

} // namespace elg

#endif // ELG_THEME_H
