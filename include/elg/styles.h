// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_STYLES_H
#define ELG_STYLES_H

// Control styles (design.md §8): controls own behavior; a style decides appearance.
//
//   struct PillButtonStyle : elg::ButtonStyle {
//       View make_body(const ButtonConfiguration& button) const override {
//           return button.label
//               .padding(EdgeInsets::symmetric(16, 8))
//               .background([=] { return button.is_pressed() ? accent_dark : accent; }, Capsule());
//       }
//   };
//
//   VStack(Button("Save", save), Button("Cancel", cancel)).button_style(PillButtonStyle{});

#include <elg/export.h>
#include <elg/view.h>

#include <functional>

namespace elg {

// What a ButtonStyle gets to build a button's appearance from. The state readers are tracked
// reads, so use them inside reactive arguments; the body itself runs once.
struct ButtonConfiguration {
    View label;
    std::function<bool()> is_pressed; // pressed with the pointer still over it
    std::function<bool()> is_hovered;
    std::function<bool()> is_enabled; // false inside .disabled(true)
};

class ELG_API ButtonStyle {
public:
    virtual ~ButtonStyle();
    // Called once per button when it's built, while Environment<K>() is available.
    virtual View make_body(const ButtonConfiguration& button) const = 0;
};

// Filled with the tint, label in ThemeColor::on_accent. The default.
class ELG_API ProminentButtonStyle final : public ButtonStyle {
public:
    View make_body(const ButtonConfiguration& button) const override;
};

// A raised surface with an outline, label in the tint.
class ELG_API BorderedButtonStyle final : public ButtonStyle {
public:
    View make_body(const ButtonConfiguration& button) const override;
};

// Just the label, in the tint, dimmed while pressed.
class ELG_API PlainButtonStyle final : public ButtonStyle {
public:
    View make_body(const ButtonConfiguration& button) const override;
};

// What a ToggleStyle gets to build a toggle's appearance from.
struct ToggleConfiguration {
    View label;
    std::function<bool()> is_on;      // a tracked read
    std::function<void()> toggle;     // flips it (animated)
    std::function<bool()> is_pressed;
    std::function<bool()> is_hovered;
    std::function<bool()> is_enabled;
};

class ELG_API ToggleStyle {
public:
    virtual ~ToggleStyle();
    // Called once per toggle when it's built, while Environment<K>() is available.
    virtual View make_body(const ToggleConfiguration& toggle) const = 0;
};

// The label, then a sliding switch in the tint. The default.
class ELG_API SwitchToggleStyle final : public ToggleStyle {
public:
    View make_body(const ToggleConfiguration& toggle) const override;
};

// A check box, then the label.
class ELG_API CheckboxToggleStyle final : public ToggleStyle {
public:
    View make_body(const ToggleConfiguration& toggle) const override;
};

// What a TextFieldStyle gets: the editable text (put it where the text goes) and its state.
struct TextFieldConfiguration {
    View field;
    std::function<bool()> is_focused;
    std::function<bool()> is_hovered;
    std::function<bool()> is_enabled;
};

class ELG_API TextFieldStyle {
public:
    virtual ~TextFieldStyle();
    virtual View make_body(const TextFieldConfiguration& field) const = 0;
};

// A raised box with an outline that turns the tint while focused. The default.
class ELG_API RoundedBorderTextFieldStyle final : public TextFieldStyle {
public:
    View make_body(const TextFieldConfiguration& field) const override;
};

// Just the text.
class ELG_API PlainTextFieldStyle final : public TextFieldStyle {
public:
    View make_body(const TextFieldConfiguration& field) const override;
};

} // namespace elg

#endif // ELG_STYLES_H
