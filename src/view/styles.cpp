// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// The environment's theme resolution and the built-in button styles. The styles use only the
// public API, like an app's own styles would (design.md §9.4, dogfooding).

#include <elg/environment.h>
#include <elg/error.h>
#include <elg/shapes.h>
#include <elg/styles.h>

namespace elg {

namespace detail {

Color resolve_theme_color(const EnvironmentValues& environment, ThemeColor token) {
    return environment.get<ThemeKey>().colors(environment.get<ColorSchemeKey>())[token];
}

void throw_needs_environment() {
    throw UsageError("this value depends on the environment (e.g. a ThemeColor); it can only be read "
                     "where it's used in a view");
}

void throw_no_environment() {
    throw UsageError("Environment<Key>() can only be used while views are built: in a body, a "
                     "ViewModifier's body, or a style's make_body");
}

} // namespace detail

std::shared_ptr<const ButtonStyle> ButtonStyleKey::default_value(const EnvironmentValues&) {
    static const auto style = std::make_shared<const ProminentButtonStyle>();
    return style;
}

ButtonStyle::~ButtonStyle() = default;

namespace {

constexpr EdgeInsets kButtonPadding = EdgeInsets::symmetric(14, 7);
constexpr float kButtonRadius = 8;

// A label font: the environment's, semibold.
Prop<Font> label_font() {
    const auto font = Environment<FontKey>();
    return [font] { return font().weight(Weight::semibold); };
}

} // namespace

View ProminentButtonStyle::make_body(const ButtonConfiguration& button) const {
    const auto tint = Environment<TintKey>();
    return button.label.font(label_font())
        .foreground_style(ThemeColor::on_accent)
        .padding(kButtonPadding)
        .background(
            [=] {
                const Color base = tint();
                if (!button.is_enabled()) {
                    return base.opacity(0.4f);
                }
                if (button.is_pressed()) {
                    return base.mix(Color::black, 0.2f);
                }
                return button.is_hovered() ? base.mix(Color::white, 0.15f) : base;
            },
            RoundedRectangle(kButtonRadius));
}

View BorderedButtonStyle::make_body(const ButtonConfiguration& button) const {
    const auto tint = Environment<TintKey>();
    const auto surface = Environment<ThemeKey>();
    const auto scheme = Environment<ColorSchemeKey>();
    const auto colors = [=] { return surface().colors(scheme()); };
    return button.label.font(label_font())
        .foreground_style([=] { return button.is_enabled() ? tint() : colors().tertiary; })
        .padding(kButtonPadding)
        .background(
            [=] {
                const Color base = colors().surface_raised;
                if (button.is_pressed()) {
                    return base.mix(colors().outline, 0.6f);
                }
                return button.is_hovered() ? base.mix(colors().outline, 0.35f) : base;
            },
            RoundedRectangle(kButtonRadius))
        .overlay(RoundedRectangle(kButtonRadius).stroke_border(ThemeColor::outline, 1));
}

View PlainButtonStyle::make_body(const ButtonConfiguration& button) const {
    const auto tint = Environment<TintKey>();
    return button.label.foreground_style([=] {
        if (!button.is_enabled()) {
            return tint().opacity(0.4f);
        }
        return button.is_pressed() ? tint().opacity(0.6f) : tint();
    });
}

} // namespace elg
