---
title: Control styles
description: "Changing how buttons, toggles and text fields look, while keeping their behavior."
---

Controls separate behavior from appearance. A `Button` handles clicks, hover, focus and keyboard
activation; a `ButtonStyle` decides what it looks like. Styles are set through the environment, so
one modifier styles every control inside a view.

## Built-in styles

| Control | Styles |
| --- | --- |
| `Button` | `ProminentButtonStyle` (default), `BorderedButtonStyle`, `PlainButtonStyle` |
| `Toggle` | `SwitchToggleStyle` (default), `CheckboxToggleStyle` |
| `TextField`, `SecureField` | `RoundedBorderTextFieldStyle` (default), `PlainTextFieldStyle` |

```cpp
Form().button_style(BorderedButtonStyle{});
Toggle("Remember me", remember).toggle_style(CheckboxToggleStyle{});
TextField("Search", query).text_field_style(PlainTextFieldStyle{});
```

## Writing a button style

A style's `make_body` gets the button's configuration, and returns its look. It runs **once** per
button; the state readers in the configuration are reactive, so use them inside closures:

```cpp
struct PillButtonStyle : ButtonStyle {
    View make_body(const ButtonConfiguration& button) const override {
        const auto tint = Environment<TintKey>();
        return button.label
            .font(Font::headline)
            .foreground_style(Color::white)
            .padding(EdgeInsets::symmetric(18, 8))
            .background([=] { return button.is_pressed() ? tint().mix(Color::black, 0.2f) : tint(); }, Capsule())
            .opacity([=] { return button.is_enabled() ? 1.f : 0.5f; });
    }
};

HStack(Button("Upgrade", upgrade), Button("Later", later)).button_style(PillButtonStyle{});
```

`ButtonConfiguration` has the `label` and four readers: `is_pressed` (pressed with the pointer
still over it), `is_hovered`, `is_enabled` and `is_focused`. `make_body` can read the environment
with `Environment<Key>()`, as above.

## Toggle styles

A `ToggleConfiguration` has the `label`, `is_on`, a `toggle()` function that flips it (animated),
and `is_pressed`, `is_hovered` and `is_enabled`:

```cpp
struct StarToggleStyle : ToggleStyle {
    View make_body(const ToggleConfiguration& toggle) const override {
        return HStack(If(toggle.is_on, Image("star.fill").foreground_style(ThemeColor::warning),
                         Image("star").foreground_style(ThemeColor::tertiary)),
                      toggle.label);
    }
};
```

The control handles the click and keyboard; the style only draws.

## Text field styles

A `TextFieldConfiguration` has the editable `field` (put it where the text goes), and
`is_focused`, `is_hovered` and `is_enabled`:

```cpp
struct UnderlinedFieldStyle : TextFieldStyle {
    View make_body(const TextFieldConfiguration& field) const override {
        return field.field.padding(EdgeInsets{.top = 6, .bottom = 6}).overlay(
            Rectangle()
                .fill([=](const EnvironmentValues& env) {
                    return field.is_focused() ? env.color(ThemeColor::accent) : env.color(ThemeColor::outline);
                })
                .frame({.height = 1}),
            Alignment::bottom);
    }
};
```
