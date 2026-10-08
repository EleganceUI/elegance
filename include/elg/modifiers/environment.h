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

namespace elg {

class ELG_API FontModifier final : public EnvironmentModifier {
public:
    explicit FontModifier(Font font) : font_(std::move(font)) {}
    void apply(EnvironmentValues& values) const override { values.font = font_; }

private:
    ELG_PRIVATE_MEMBERS_BEGIN
    Font font_;
    ELG_PRIVATE_MEMBERS_END
};

class ELG_API ForegroundStyleModifier final : public EnvironmentModifier {
public:
    explicit ForegroundStyleModifier(Color color) noexcept : color_(color) {}
    void apply(EnvironmentValues& values) const override { values.foreground = color_; }

private:
    Color color_;
};

class ELG_API TintModifier final : public EnvironmentModifier {
public:
    explicit TintModifier(Color color) noexcept : color_(color) {}
    void apply(EnvironmentValues& values) const override { values.tint = color_; }

private:
    Color color_;
};

} // namespace elg

#endif // ELG_MODIFIERS_ENVIRONMENT_H
