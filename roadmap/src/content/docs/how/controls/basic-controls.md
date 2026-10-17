---
title: Button, Toggle, Slider, Stepper, ProgressView, Picker
description: "How controls own their behavior and state, hand appearance to styles, and respond to pointer and keyboard."
sidebar:
  order: 1
depends: [framework/input-and-focus, framework/environment-and-styles, framework/animation, text/fonts-and-layout]
---

## What it's for

Controls are where behavior lives: what a click means, which keys work, when a control is
pressed, hovered, focused or disabled. Appearance belongs to styles. Getting the split right
means every control can be restyled without being reimplemented, and every control works by
keyboard.

## How it works

### The pattern

Every control follows the same shape:

1. The control's **element** handles input and owns its interaction state as `State<bool>`s:
   pressed, hovered, focused.
2. It finds its **style** in the environment and calls `make_body(configuration)`, which holds the
   label and those states, once, at mount.
3. The style returns ordinary views whose reactive arguments read the states. A press flips one
   `State`, which re-runs only the effects that read it: a color, an offset.
4. The element joins the window's focus and reads `DisabledKey` to ignore input when disabled.

### Button

`ButtonElement` is the template for everything else.

- **Pointer**: a down inside sets `pressed` and captures the pointer. Moving out clears `pressed`
  and moving back sets it. An up inside runs the action; an up outside doesn't.
- **Hover**: with the environment's hover delay, so passing over doesn't flicker.
- **Keyboard**: Enter activates on press. Space sets `pressed` on key down and activates on key
  up, as native buttons do. Extra activation keys can be added (Right opens a submenu row).
- **Focus**: focusable, with the ring in the tint; `ButtonConfiguration::is_focused` lets a style
  show focus its own way.
- **Built-in styles**: `Prominent` (filled with the tint, with distinct hovered, pressed, disabled
  and focused looks), `Bordered`, and `Plain`. A style's focus look is a 2 pt ring in the tint
  just outside its outline.

### Toggle

`Toggle(label, Binding<bool>)` reuses the button's behavior, with the action "flip the binding".
`ToggleStyle` builds the look from the label and `is_on`.

- `SwitchToggleStyle` (default): a capsule track and a circular knob moved with `.offset`. The
  flip happens inside an animated transaction, so the knob slides and the track color blends.
- `CheckboxToggleStyle`: a rounded square that shows a ✓ icon when on.

### Slider

`Slider(Binding<float>, {.min, .max, .step, .on_editing_changed})`:

- A press anywhere on the track jumps the value there and starts a drag. Moving maps the pointer's
  x across the track to [min, max], snapped to `step`. `on_editing_changed(true/false)` brackets
  the drag.
- Focused: arrows move by `step` (or a twentieth of the range without one), Page Up/Down by the
  larger of that and a tenth of the range, Home/End to the ends. Key changes are animated.
- Every write is snapped to `step` (from `min`) and clamped to the range.
- The knob's position follows the binding, so outside changes move it, animated during animated
  updates.

### Stepper

`Stepper(label, Binding<int>, {.min, .max, .step})` is **composed** from the public API: an
`HStack` of the label, a Spacer, and a joined pair of −/+ Buttons (in their own segment style)
that add or subtract `step`, each disabled at its bound. Composing it from public
parts is deliberate dogfooding.

### ProgressView

- `ProgressView(Prop<float>)`: a track and a fill sized to the fraction, which animates when the
  value changes in an animated update.
- `ProgressView()`: indeterminate. It sets the display list's `animating` flag while shown, so the
  window keeps rendering only while one is visible.

### Picker

`Picker(Binding<T>, {{value, "Label"}, ...})`:

- **Segmented** (default): a row of Buttons. The selected one is highlighted; changing it is
  animated.
- **Menu** (`PickerStyle::menu`): a pop-up button showing the current label, opening a menu of
  checked items (see [Popups, menus, and tooltips](../popups-and-menus/)).

## Rules

- Controls own state; styles only read it.
- Everything works by keyboard.
- Disabled means no input and a disabled look, from one environment value.
- Built-in styles use only the public API.

## Edge cases

- **The action runs during event handling**, so it may write state freely. If it opens a popup,
  the popup opens after the event (requests are queued).
- **Button inside a moving parent**: the "released inside" test uses the un-offset frame (a known
  gap).
- **Slider with `max <= min`**: the range is empty. Decide explicitly (clamp to `min`, ignore
  input) rather than letting the fraction divide by zero.
