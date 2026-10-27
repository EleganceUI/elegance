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

#include <elg/animation.h>
#include <elg/export.h>
#include <elg/view.h>

#include <functional>
#include <optional>

namespace elg {

// What a ButtonStyle gets to build a button's appearance from. The state readers are tracked
// reads, so use them inside reactive arguments; the body itself runs once.
struct ButtonConfiguration {
    View label;
    Signal<bool> is_pressed; // pressed with the pointer still over it
    Signal<bool> is_hovered;
    Signal<bool> is_enabled; // false inside .disabled(true)
    Signal<bool> is_focused; // has keyboard focus
};

class ELG_API ButtonStyle {
public:
    virtual ~ButtonStyle();
    // Called once per button when it's built, while Environment<K>() is available.
    virtual View make_body(const ButtonConfiguration& button) const = 0;

protected:
    // Styles are copied as their concrete type, never as a ButtonStyle.
    ButtonStyle() = default;
    ButtonStyle(const ButtonStyle&) = default;
    ButtonStyle(ButtonStyle&&) = default;
    ButtonStyle& operator=(const ButtonStyle&) = default;
    ButtonStyle& operator=(ButtonStyle&&) = default;
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

// Just the label, in the colors around it (not the tint), dimmed while pressed or disabled.
class ELG_API PlainButtonStyle final : public ButtonStyle {
public:
    View make_body(const ButtonConfiguration& button) const override;
};

// What a ToggleStyle gets to build a toggle's appearance from.
struct ToggleConfiguration {
    View label;
    Signal<bool> is_on;           // a tracked read
    std::function<void()> toggle; // flips it (with the style's animation())
    Signal<bool> is_pressed;
    Signal<bool> is_hovered;
    Signal<bool> is_enabled;
    Signal<bool> is_focused; // has keyboard focus
};

class ELG_API ToggleStyle {
public:
    virtual ~ToggleStyle();
    // Called once per toggle when it's built, while Environment<K>() is available.
    virtual View make_body(const ToggleConfiguration& toggle) const = 0;
    // How a flip animates; nullopt: at once.
    virtual std::optional<Animation> animation() const { return Animation::smooth(0.3); }

protected:
    // Styles are copied as their concrete type, never as a ToggleStyle.
    ToggleStyle() = default;
    ToggleStyle(const ToggleStyle&) = default;
    ToggleStyle(ToggleStyle&&) = default;
    ToggleStyle& operator=(const ToggleStyle&) = default;
    ToggleStyle& operator=(ToggleStyle&&) = default;
};

struct SwitchToggleStyleOptions {
    std::optional<Prop<Color>> on;         // the track while on; nullopt: the tint
    Prop<Color> off = ThemeColor::outline; // the track while off
    Prop<Color> knob = Color::white;
    float width = 40;
    float height = 24;
    float knob_inset = 2; // between the knob and the track's edge
    float spacing = 8;    // between the label and the switch
    // The label at the leading edge and the switch at the trailing one, across the width it's
    // offered (a settings row). Otherwise it's as wide as the label and switch.
    bool fills_width = false;
    std::optional<Animation> animation = Animation::smooth(0.3); // nullopt: flips at once
};

// The label, then a sliding switch in the tint. The default.
//   Toggle("Wi-Fi", wifi).toggle_style(SwitchToggleStyle{{.on = ThemeColor::success, .fills_width = true}})
class ELG_API SwitchToggleStyle final : public ToggleStyle {
public:
    explicit SwitchToggleStyle(SwitchToggleStyleOptions options = {}) : options_(std::move(options)) {}
    View make_body(const ToggleConfiguration& toggle) const override;
    std::optional<Animation> animation() const override { return options_.animation; }

private:
    SwitchToggleStyleOptions options_;
};

// What a CheckboxStyle gets to build a check box's appearance from.
struct CheckboxConfiguration {
    View label;
    Signal<bool> is_checked;      // a tracked read
    Signal<bool> is_mixed;        // some of what it stands for is checked (CheckboxOptions::mixed)
    std::function<void()> toggle; // checks or unchecks it; a mixed one becomes checked
    Signal<bool> is_pressed;
    Signal<bool> is_hovered;
    Signal<bool> is_enabled;
    Signal<bool> is_focused; // has keyboard focus
};

class ELG_API CheckboxStyle {
public:
    virtual ~CheckboxStyle();
    // Called once per check box when it's built, while Environment<K>() is available.
    virtual View make_body(const CheckboxConfiguration& checkbox) const = 0;
    // How checking and unchecking animate; nullopt: at once.
    virtual std::optional<Animation> animation() const { return Animation::smooth(0.3); }

protected:
    // Styles are copied as their concrete type, never as a CheckboxStyle.
    CheckboxStyle() = default;
    CheckboxStyle(const CheckboxStyle&) = default;
    CheckboxStyle(CheckboxStyle&&) = default;
    CheckboxStyle& operator=(const CheckboxStyle&) = default;
    CheckboxStyle& operator=(CheckboxStyle&&) = default;
};

struct BoxCheckboxStyleOptions {
    float size = 18;
    float corner_radius = 5;
    std::optional<Prop<Color>> fill;                // checked or mixed; nullopt: the tint
    Prop<Color> empty = ThemeColor::surface_raised; // unchecked
    Prop<Color> border = ThemeColor::outline;       // unchecked; checked or mixed, it's the fill
    float border_width = 1.5f;
    Prop<Color> mark = ThemeColor::on_accent; // the check and the dash
    std::optional<View> check;                // shown while checked; nullopt: a check mark
    std::optional<View> dash;                 // shown while mixed; nullopt: a dash
    float spacing = 8;                        // between the box and the label
    std::optional<Animation> animation = Animation::smooth(0.3); // nullopt: changes at once
};

// A box, then the label: filled in the tint with a check mark while checked, a dash while mixed.
// The default.
//   Checkbox("Agree", agreed).checkbox_style(BoxCheckboxStyle{{.size = 16, .corner_radius = 3}})
class ELG_API BoxCheckboxStyle final : public CheckboxStyle {
public:
    explicit BoxCheckboxStyle(BoxCheckboxStyleOptions options = {}) : options_(std::move(options)) {}
    View make_body(const CheckboxConfiguration& checkbox) const override;
    std::optional<Animation> animation() const override { return options_.animation; }

private:
    BoxCheckboxStyleOptions options_;
};

// What a NumberInputStyle gets: the editable number (put it where the text goes), the steps,
// and the state.
struct NumberInputConfiguration {
    View field;
    std::function<void()> increment; // a step up (or down), within the range
    std::function<void()> decrement;
    Signal<bool> can_increment; // below the maximum (above the minimum)
    Signal<bool> can_decrement;
    Signal<bool> shows_buttons; // NumberInputOptions::shows_buttons
    Signal<bool> is_focused;    // being edited
    Signal<bool> is_hovered;
    Signal<bool> is_enabled;
};

class ELG_API NumberInputStyle {
public:
    virtual ~NumberInputStyle();
    // Called once per number input when it's built, while Environment<K>() is available.
    virtual View make_body(const NumberInputConfiguration& input) const = 0;

protected:
    // Styles are copied as their concrete type, never as a NumberInputStyle.
    NumberInputStyle() = default;
    NumberInputStyle(const NumberInputStyle&) = default;
    NumberInputStyle(NumberInputStyle&&) = default;
    NumberInputStyle& operator=(const NumberInputStyle&) = default;
    NumberInputStyle& operator=(NumberInputStyle&&) = default;
};

struct BoxNumberInputStyleOptions {
    Prop<Color> background = ThemeColor::surface_raised;
    Prop<Color> border = ThemeColor::outline;
    std::optional<Prop<Color>> focused_border; // nullopt: the tint
    float border_width = 1;
    float corner_radius = 6;
    EdgeInsets padding = EdgeInsets::symmetric(8, 5);
    float arrow_size = 9; // the stacked arrows' icons
};

// A raised box with an outline (the tint while editing), and stacked arrows at its trailing
// edge. The default.
//   NumberInput(size, {.min = 8}).number_input_style(BoxNumberInputStyle{{.corner_radius = 4}})
class ELG_API BoxNumberInputStyle final : public NumberInputStyle {
public:
    explicit BoxNumberInputStyle(BoxNumberInputStyleOptions options = {}) : options_(std::move(options)) {}
    View make_body(const NumberInputConfiguration& input) const override;

private:
    BoxNumberInputStyleOptions options_;
};

// Just the number, and the arrows if shown: for putting it in a box of your own.
class ELG_API PlainNumberInputStyle final : public NumberInputStyle {
public:
    View make_body(const NumberInputConfiguration& input) const override;
};

// What a ProgressStyle gets: how far along it is.
struct ProgressConfiguration {
    Signal<std::optional<float>> fraction; // 0...1; empty: indeterminate (activity of unknown length)
};

class ELG_API ProgressStyle {
public:
    virtual ~ProgressStyle();
    // Called once per Progress when it's built, while Environment<K>() is available.
    virtual View make_body(const ProgressConfiguration& progress) const = 0;

protected:
    // Styles are copied as their concrete type, never as a ProgressStyle.
    ProgressStyle() = default;
    ProgressStyle(const ProgressStyle&) = default;
    ProgressStyle(ProgressStyle&&) = default;
    ProgressStyle& operator=(const ProgressStyle&) = default;
    ProgressStyle& operator=(ProgressStyle&&) = default;
};

struct LinearProgressStyleOptions {
    Prop<Color> track = ThemeColor::outline;
    std::optional<Prop<Color>> fill; // nullopt: the tint
    float thickness = 6;
};

// A bar as wide as offered, filled to the fraction, or with a segment sweeping across while
// indeterminate. The default.
//   Progress(upload.fraction()).progress_style(LinearProgressStyle{{.thickness = 3}})
class ELG_API LinearProgressStyle final : public ProgressStyle {
public:
    explicit LinearProgressStyle(LinearProgressStyleOptions options = {}) : options_(std::move(options)) {}
    View make_body(const ProgressConfiguration& progress) const override;

private:
    LinearProgressStyleOptions options_;
};

struct CircularProgressStyleOptions {
    Prop<Color> track = ThemeColor::outline;
    std::optional<Prop<Color>> fill; // nullopt: the tint
    float size = 20;      // the ring's diameter
    float line_width = 3;
};

// A ring filled clockwise from the top to the fraction, or with an arc spinning while
// indeterminate.
//   Progress().progress_style(CircularProgressStyle{{.size = 14, .line_width = 2}})
class ELG_API CircularProgressStyle final : public ProgressStyle {
public:
    explicit CircularProgressStyle(CircularProgressStyleOptions options = {}) : options_(std::move(options)) {}
    View make_body(const ProgressConfiguration& progress) const override;

private:
    CircularProgressStyleOptions options_;
};

// What a TextFieldStyle gets: the editable text (put it where the text goes) and its state.
struct TextFieldConfiguration {
    View field;
    Signal<bool> is_focused;
    Signal<bool> is_hovered;
    Signal<bool> is_enabled;
};

class ELG_API TextFieldStyle {
public:
    virtual ~TextFieldStyle();
    virtual View make_body(const TextFieldConfiguration& field) const = 0;

protected:
    // Styles are copied as their concrete type, never as a TextFieldStyle.
    TextFieldStyle() = default;
    TextFieldStyle(const TextFieldStyle&) = default;
    TextFieldStyle(TextFieldStyle&&) = default;
    TextFieldStyle& operator=(const TextFieldStyle&) = default;
    TextFieldStyle& operator=(TextFieldStyle&&) = default;
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

// What a PopoverStyle wraps in a panel: the popover's content.
struct PopoverConfiguration {
    View content;
    Signal<bool> is_padded; // PopoverOptions::padded: false lets the content reach the edges
};

class ELG_API PopoverStyle {
public:
    virtual ~PopoverStyle();
    // Called each time it opens, while Environment<K>() is available.
    virtual View make_body(const PopoverConfiguration& popover) const = 0;
    // How it opens and closes; at once by default.
    virtual PopupMotion motion() const { return PopupMotion::none(); }

protected:
    // Styles are copied as their concrete type, never as a PopoverStyle.
    PopoverStyle() = default;
    PopoverStyle(const PopoverStyle&) = default;
    PopoverStyle(PopoverStyle&&) = default;
    PopoverStyle& operator=(const PopoverStyle&) = default;
    PopoverStyle& operator=(PopoverStyle&&) = default;
};

// A rounded surface with an outline and a shadow; padded content is inset 12 points. The default.
class ELG_API DefaultPopoverStyle final : public PopoverStyle {
public:
    View make_body(const PopoverConfiguration& popover) const override;
};

// Just the content: no panel, padding or shadow.
class ELG_API PlainPopoverStyle final : public PopoverStyle {
public:
    View make_body(const PopoverConfiguration& popover) const override;
};

// What a TooltipStyle wraps: the tooltip's content.
struct TooltipConfiguration {
    View content; // .tooltip(text): the text
};

class ELG_API TooltipStyle {
public:
    virtual ~TooltipStyle();
    // Called each time it opens, while Environment<K>() is available.
    virtual View make_body(const TooltipConfiguration& tooltip) const = 0;
    // How it opens and closes; at once by default.
    virtual PopupMotion motion() const { return PopupMotion::none(); }

protected:
    // Styles are copied as their concrete type, never as a TooltipStyle.
    TooltipStyle() = default;
    TooltipStyle(const TooltipStyle&) = default;
    TooltipStyle(TooltipStyle&&) = default;
    TooltipStyle& operator=(const TooltipStyle&) = default;
    TooltipStyle& operator=(TooltipStyle&&) = default;
};

// Small text on a rounded surface with an outline and a shadow. The default.
class ELG_API DefaultTooltipStyle final : public TooltipStyle {
public:
    View make_body(const TooltipConfiguration& tooltip) const override;
};

// Just the content.
class ELG_API PlainTooltipStyle final : public TooltipStyle {
public:
    View make_body(const TooltipConfiguration& tooltip) const override;
};

// What a MenuStyle wraps in a panel: a Menu's items.
struct MenuConfiguration {
    View items; // the rows, stacked; rows span the panel's width
};

class ELG_API MenuStyle {
public:
    virtual ~MenuStyle();
    // Called each time it opens, while Environment<K>() is available.
    virtual View make_body(const MenuConfiguration& menu) const = 0;
    // How it opens and closes; at once by default.
    virtual PopupMotion motion() const { return PopupMotion::none(); }

protected:
    // Styles are copied as their concrete type, never as a MenuStyle.
    MenuStyle() = default;
    MenuStyle(const MenuStyle&) = default;
    MenuStyle(MenuStyle&&) = default;
    MenuStyle& operator=(const MenuStyle&) = default;
    MenuStyle& operator=(MenuStyle&&) = default;
};

// A surface with an outline and a shadow, at least 200 points wide. The default.
class ELG_API DefaultMenuStyle final : public MenuStyle {
public:
    View make_body(const MenuConfiguration& menu) const override;
};

// Just the items.
class ELG_API PlainMenuStyle final : public MenuStyle {
public:
    View make_body(const MenuConfiguration& menu) const override;
};

} // namespace elg

#endif // ELG_STYLES_H
