// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_THEME_H
#define ELG_THEME_H

// Themes (design.md §8): semantic color tokens with light and dark variants, and a scale of blur
// radii.

#include <elg/color.h>

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
    // Frosted glass: translucent tints for under a .backdrop_blur(), thinnest to thickest. Light
    // in light mode and dark in dark mode by default:
    //   Text("Now playing").padding(12)
    //       .background(ThemeColor::glass_regular, RoundedRectangle(12))
    //       .backdrop_blur(ThemeBlur::xl, RoundedRectangle(12), {.saturation = 1.8f, .noise = 0.02f})
    glass_ultra_thin,
    glass_thin,
    glass_regular,
    glass_thick,
    glass_ultra_thick,
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
    Color glass_ultra_thin;
    Color glass_thin;
    Color glass_regular;
    Color glass_thick;
    Color glass_ultra_thick;

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
        case ThemeColor::glass_ultra_thin: return glass_ultra_thin;
        case ThemeColor::glass_thin: return glass_thin;
        case ThemeColor::glass_regular: return glass_regular;
        case ThemeColor::glass_thick: return glass_thick;
        case ThemeColor::glass_ultra_thick: return glass_ultra_thick;
        }
        return primary;
    }
};

// Blur radii, smallest to largest, resolved against the environment's theme where they're used
// (.blur(), .backdrop_blur()). xxl and xxxl are 2xl and 3xl.
//   Image({.file = "cover.jpg"}).blur(ThemeBlur::sm)
enum class ThemeBlur { xs, sm, md, lg, xl, xxl, xxxl };

// In points.
struct BlurTokens {
    float xs = 4;
    float sm = 8;
    float md = 12;
    float lg = 16;
    float xl = 24;
    float xxl = 40;
    float xxxl = 64;

    constexpr float operator[](ThemeBlur token) const noexcept {
        switch (token) {
        case ThemeBlur::xs: return xs;
        case ThemeBlur::sm: return sm;
        case ThemeBlur::md: return md;
        case ThemeBlur::lg: return lg;
        case ThemeBlur::xl: return xl;
        case ThemeBlur::xxl: return xxl;
        case ThemeBlur::xxxl: return xxxl;
        }
        return md;
    }
};

struct Theme {
    ColorTokens light;
    ColorTokens dark;
    BlurTokens blur;

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
                .glass_ultra_thin = Color{0.97f, 0.97f, 0.98f, 0.2f},
                .glass_thin = Color{0.97f, 0.97f, 0.98f, 0.38f},
                .glass_regular = Color{0.97f, 0.97f, 0.98f, 0.55f},
                .glass_thick = Color{0.97f, 0.97f, 0.98f, 0.72f},
                .glass_ultra_thick = Color{0.97f, 0.97f, 0.98f, 0.86f},
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
                .glass_ultra_thin = Color{0.11f, 0.11f, 0.12f, 0.3f},
                .glass_thin = Color{0.11f, 0.11f, 0.12f, 0.45f},
                .glass_regular = Color{0.11f, 0.11f, 0.12f, 0.6f},
                .glass_thick = Color{0.11f, 0.11f, 0.12f, 0.74f},
                .glass_ultra_thick = Color{0.11f, 0.11f, 0.12f, 0.86f},
            },
        };
    }
};

} // namespace elg

#endif // ELG_THEME_H
