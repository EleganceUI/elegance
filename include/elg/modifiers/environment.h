// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_MODIFIERS_ENVIRONMENT_H
#define ELG_MODIFIERS_ENVIRONMENT_H

// Built-in environment modifiers. View::font() and foreground_style() are sugar over these.

#include <elg/color.h>
#include <elg/environment.h>
#include <elg/export.h>
#include <elg/font.h>
#include <elg/modifier.h>
#include <elg/prop.h>

#include <memory>
#include <utility>

namespace elg {

/// Sets one environment key. The built-in environment modifiers are this template; so is
/// View::environment<Key>(value).
///
/// @tparam Key The key (elg/environment.h): a type with a `Value` type, a `name` and a
///   `default_value()`.
template <class Key>
class EnvironmentKeyModifier final : public EnvironmentModifier {
public:
    /// Sets `Key` to `value`.
    ///
    /// @param value The value for everything inside. Reactive.
    explicit EnvironmentKeyModifier(Prop<typename Key::Value> value) : value_(std::move(value)) {}
    /// Sets `Key` in `values`.
    ///
    /// @param values A copy of the environment from outside the view, to change.
    void apply(EnvironmentValues& values) const override { values.template set<Key>(value_); }

private:
    Prop<typename Key::Value> value_;
};

/// Sets FontKey: View::font() and font_weight().
using FontModifier = EnvironmentKeyModifier<FontKey>;
/// Sets ForegroundKey: View::foreground_style().
using ForegroundStyleModifier = EnvironmentKeyModifier<ForegroundKey>;
/// Sets TintKey: View::tint().
using TintModifier = EnvironmentKeyModifier<TintKey>;
/// Sets ColorSchemeKey: View::color_scheme().
using ColorSchemeModifier = EnvironmentKeyModifier<ColorSchemeKey>;
/// Sets ThemeKey: View::theme().
using ThemeModifier = EnvironmentKeyModifier<ThemeKey>;
/// Sets DisabledKey: View::disabled().
using DisabledModifier = EnvironmentKeyModifier<DisabledKey>;
/// Sets ButtonStyleKey: View::button_style().
using ButtonStyleModifier = EnvironmentKeyModifier<ButtonStyleKey>;
/// Sets HoverDelayKey: View::hover_delay().
using HoverDelayModifier = EnvironmentKeyModifier<HoverDelayKey>;
/// Sets ToggleStyleKey: View::toggle_style().
using ToggleStyleModifier = EnvironmentKeyModifier<ToggleStyleKey>;
/// Sets CheckboxStyleKey: View::checkbox_style().
using CheckboxStyleModifier = EnvironmentKeyModifier<CheckboxStyleKey>;
/// Sets NumberInputStyleKey: View::number_input_style().
using NumberInputStyleModifier = EnvironmentKeyModifier<NumberInputStyleKey>;
/// Sets ProgressStyleKey: View::progress_style().
using ProgressStyleModifier = EnvironmentKeyModifier<ProgressStyleKey>;
/// Sets PickerStyleKey: View::picker_style().
using PickerStyleModifier = EnvironmentKeyModifier<PickerStyleKey>;
/// Sets SliderStyleKey: View::slider_style().
using SliderStyleModifier = EnvironmentKeyModifier<SliderStyleKey>;
/// Sets PopoverStyleKey: View::popover_style().
using PopoverStyleModifier = EnvironmentKeyModifier<PopoverStyleKey>;
/// Sets TooltipStyleKey: View::tooltip_style().
using TooltipStyleModifier = EnvironmentKeyModifier<TooltipStyleKey>;
/// Sets MenuStyleKey: View::menu_style().
using MenuStyleModifier = EnvironmentKeyModifier<MenuStyleKey>;
/// Sets NavigationStackStyleKey: View::navigation_stack_style().
using NavigationStackStyleModifier = EnvironmentKeyModifier<NavigationStackStyleKey>;
/// Sets TextFieldStyleKey: View::text_field_style().
using TextFieldStyleModifier = EnvironmentKeyModifier<TextFieldStyleKey>;
/// Sets RowStyleKey: View::row_style().
using RowStyleModifier = EnvironmentKeyModifier<RowStyleKey>;

} // namespace elg

#endif // ELG_MODIFIERS_ENVIRONMENT_H
