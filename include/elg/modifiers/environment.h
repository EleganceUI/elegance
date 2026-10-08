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

// Sets one environment key. The built-in environment modifiers are this template; so is
// View::environment<Key>(value).
template <class Key>
class EnvironmentKeyModifier final : public EnvironmentModifier {
public:
    explicit EnvironmentKeyModifier(Prop<typename Key::Value> value) : value_(std::move(value)) {}
    void apply(EnvironmentValues& values) const override { values.template set<Key>(value_); }

private:
    Prop<typename Key::Value> value_;
};

using FontModifier = EnvironmentKeyModifier<FontKey>;
using ForegroundStyleModifier = EnvironmentKeyModifier<ForegroundKey>;
using TintModifier = EnvironmentKeyModifier<TintKey>;
using ColorSchemeModifier = EnvironmentKeyModifier<ColorSchemeKey>;
using ThemeModifier = EnvironmentKeyModifier<ThemeKey>;
using DisabledModifier = EnvironmentKeyModifier<DisabledKey>;
using ButtonStyleModifier = EnvironmentKeyModifier<ButtonStyleKey>;
using HoverDelayModifier = EnvironmentKeyModifier<HoverDelayKey>;

} // namespace elg

#endif // ELG_MODIFIERS_ENVIRONMENT_H
