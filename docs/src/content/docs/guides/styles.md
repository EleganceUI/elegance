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
| `Toggle` | `SwitchToggleStyle` (default) |
| `Checkbox` | `BoxCheckboxStyle` (default) |
| `NumberInput` | `BoxNumberInputStyle` (default), `PlainNumberInputStyle` |
| `Progress` | `LinearProgressStyle` (default), `CircularProgressStyle` |
| `TextField`, `TextArea` | `RoundedBorderTextFieldStyle` (default), `PlainTextFieldStyle` |
| `popover` | `DefaultPopoverStyle` (default), `PlainPopoverStyle` |
| `tooltip` | `DefaultTooltipStyle` (default), `PlainTooltipStyle` |
| `Menu` | `DefaultMenuStyle` (default), `PlainMenuStyle` |

```cpp
Form().button_style(BorderedButtonStyle{});
Toggle("Wi-Fi", wifi).toggle_style(SwitchToggleStyle{{.fills_width = true}});
Checkbox("Remember me", remember).checkbox_style(BoxCheckboxStyle{{.size = 16}});
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

The readers are `Signal<bool>`s: read-only values that change. Call one inside a closure to follow
it, or pass it straight to anything that takes a `Prop<bool>`, like `If(button.is_hovered, ...)`.

## Toggle styles

A `ToggleConfiguration` has the `label`, `is_on`, a `toggle()` function that flips it, and
`is_pressed`, `is_hovered`, `is_enabled` and `is_focused`. Override `animation()` to change how a
flip animates, or return `std::nullopt` to flip at once:

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

## Checkbox styles

A `CheckboxConfiguration` has the same parts, with `is_checked` for `is_on`, and `is_mixed` for
a check box that's partly checked (`CheckboxOptions::mixed`):

```cpp
struct RadioLikeCheckboxStyle : CheckboxStyle {
    View make_body(const CheckboxConfiguration& box) const override {
        return HStack({.spacing = 8},
                      Circle()
                          .stroke_border(ThemeColor::outline, 1.5f)
                          .overlay(Circle().fill(ThemeColor::accent).padding(4).opacity([=] {
                              return box.is_checked() || box.is_mixed() ? 1.f : 0.f;
                          }))
                          .frame(18, 18),
                      box.label);
    }
};
```

Toggle and Checkbox share one implementation, so a style can't change how they respond to input.

A control made without a label (`Toggle(binding)`, `Checkbox(binding)`) gets an empty one;
check `label.is_empty()` to leave out the space you'd put beside it.

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

## Popover, tooltip and menu styles

These draw the panel around popup content: a `PopoverConfiguration` has the `content` and
`is_padded`, a `TooltipConfiguration` the `content`, and a `MenuConfiguration` the stacked
`items`. The style where the view that opens it is placed applies, so one `.popover_style(...)`
near the root styles every popover in the window. The plain styles draw nothing around the content.
A style's `motion()` says how its popups open and close (at once, by default); see
[Menus and popups](../menus/#opening-and-closing).

```cpp
struct CardPopoverStyle : PopoverStyle {
    View make_body(const PopoverConfiguration& popover) const override {
        return popover.content.padding(16)
            .background(ThemeColor::surface_raised, RoundedRectangle(16))
            .shadow({.color = Color::black.opacity(0.2f), .radius = 24, .y = 8});
    }
};

Root().popover_style(CardPopoverStyle{}).tooltip_style(PlainTooltipStyle{});
```
