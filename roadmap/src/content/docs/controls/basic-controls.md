---
title: Button, Toggle, Slider, Stepper, ProgressView, Picker
description: The core controls, each with behavior separate from its style.
sidebar:
  order: 1
---

Controls own behavior and state (hover, press, focus, enabled). Their look comes from a style
built through the public API, so users can replace it.

:::note[Depends on]
[Hit testing, events, and focus](../../framework/input-and-focus/),
[Environment, themes, and styles](../../framework/environment-and-styles/),
[Animation and transitions](../../framework/animation/), and text.
:::

## Read first

- `design.md` §13 (Component Library), M1 Button, M4 controls
- `include/elg/views.h`, `include/elg/styles.h`, `src/view/controls.cpp`, `ButtonElement` in `src/view/element.*`

## Tasks

### Button
- [ ] `Button(View label, action)` and `Button(title, action)`
- [ ] `ButtonElement` owns `pressed` / `hovered` States, which the style reads
- [ ] Activates on release inside; follows the environment's hover delay
- [ ] Focusable: Enter activates on press; Space shows pressed and activates on release
- [ ] `ButtonConfiguration::is_focused` so styles can show keyboard focus
- [ ] `.tint(...)` environment value; the default style uses tint, hover and pressed states
- [ ] Respect `.disabled()`

### Toggle
- [ ] `Toggle(label, Binding<bool>)` reusing the button's behavior (click, Space, Enter)
- [ ] `ToggleStyle` protocol: `SwitchToggleStyle` (default; the knob moves with `.offset`, animated) and `CheckboxToggleStyle` (a real ✓ once icons exist)
- [ ] `.toggle_style(...)`

### Slider
- [ ] `Slider(Binding<float>, {.min, .max, .step, .on_editing_changed})`
- [ ] Drag anywhere on it; arrows, Page Up/Down and Home/End when focused (key changes animate)
- [ ] The knob follows the binding, animated during animated updates

### Stepper
- [ ] `Stepper(label, Binding<int>, {.min, .max, .step})`, composed from the public API
- [ ] −/+ disabled at the bounds; named "Decrease" / "Increase" for accessibility

### ProgressView
- [ ] `ProgressView(Prop<float>)`: determinate, animates with its value
- [ ] `ProgressView()`: indeterminate, rendering continuously only while shown

### Picker
- [ ] `Picker(Binding<T>, {{value, "Label"}, ...})` segmented, built from Buttons; the selection change animates
- [ ] `PickerStyle::menu` once popups exist ([Popups, menus, and tooltips](../popups-and-menus/))

## Done when

- [ ] Every control works by mouse and by keyboard alone
- [ ] A custom `ButtonStyle` and `ToggleStyle` replace the built-ins without framework changes
- [ ] Disabled controls ignore input and look disabled

## Gotchas

- Not built yet: Stepper auto-repeat while held; style protocols for Slider and ProgressView; a
  sliding selection indicator for the segmented picker (needs matched geometry); `Link`, `Badge`,
  `DatePicker`, `ColorPicker`, `Gauge`, `.redacted`, `DisclosureGroup`, `Table`, `OutlineGroup`,
  `TabView`, `NavigationStack`, `NavigationSplitView`, `HSplitView`, `Toast`.
