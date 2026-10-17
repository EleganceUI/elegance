---
title: Environment, themes, and styles
description: "Inherited values without a cascade, theme tokens resolved where they're used, and style protocols for controls."
sidebar:
  order: 5
depends: [framework/shapes-and-effects]
---

## What it's for

Apps need consistent fonts, colors and control looks without repeating them on every view.
Elegence gets that from three mechanisms instead of stylesheets: **environment values**
inherited down the tree, **themes** of semantic color tokens, and **style protocols** that let
any control's appearance be replaced while it keeps its behavior.

## How it works

### Environment values

A key is a type:

```cpp
struct CompactKey {
    using Value = bool;
    static constexpr std::string_view name = "myapp.compact";   // stable, unique
    static bool default_value(const elg::EnvironmentValues&) { return false; }
};
```

`EnvironmentValues` is a **persistent linked list** of `(name, Prop<Value>)` entries.
`set<Key>(value)` prepends a new entry; earlier copies still point at the old head, so copying an
environment is one pointer and children never affect parents. `get<Key>()` walks the list
comparing **names** (stable strings, so they work across hot-reloaded DLLs) and falls back to
`Key::default_value(*this)`.

Values are `Prop`s, so an environment value can be static, a `State`, or a closure. Reading one
is a tracked read: only the effects that read a key re-run when it changes.

When a value depends on the environment itself (a `ThemeColor`, or a closure taking
`const EnvironmentValues&`), `set` **resolves it against the environment it was set in**. This
prevents a key's value from depending on itself (e.g. "foreground = the theme's primary color"
evaluated where foreground is being looked up).

### Where it applies

Lowering carries an environment in its context. An `EnvironmentModifier` copies it, applies its
change, and lowers the content with the copy. `Environment<Key>()` reads the environment being
lowered (through `EnvironmentScope`), which is the placement environment inside component bodies,
ViewModifier bodies and style `make_body`. A plain function returning a View runs before
placement and sees its caller's environment.

Built-in keys: font, foreground, tint, color scheme, theme, disabled, hover delay, and the button,
toggle and text-field styles.

### Themes

A `Theme` holds light and dark `ColorTokens`. `ThemeColor` names a token (`primary`,
`secondary`, `accent`, `background`, `outline`, `danger`, ...). A `ThemeColor` is accepted
anywhere a color is, and resolves **where it's used**: `resolve_theme_color(env, token)` reads the
theme and the color scheme from the environment (both tracked reads). Defaults are defined the
same way; `ForegroundKey` defaults to the theme's `primary`, and `TintKey` to `accent`.

Each window starts its environment from the system's light/dark setting, held in a `State` the
platform's theme-changed event updates. Switching system appearance re-runs only the effects
that read colors. `.color_scheme(...)` and `.theme(...)` override for a subtree.

### Styles

A control separates **behavior** (it owns hover, press, focus and enabled state, and handles
input) from **appearance** (a style builds its look). For buttons:

```cpp
struct ButtonStyle {
    virtual View make_body(ButtonConfiguration config) const = 0;
};
```

`ButtonConfiguration` carries the label view and the states as reactive values (`is_pressed()`,
`is_hovered()`, `is_focused()`, `is_enabled()`). The style returns ordinary views that read them
in reactive arguments, so a press changes one color without rebuilding anything. The style is
found through the environment (`.button_style(...)` sets it for a subtree). Built-in styles
(`Prominent`, `Bordered`, `Plain`) use only the public API. `ToggleStyle` and `TextFieldStyle`
follow the same pattern.

`.disabled(Prop<bool>)` sets `DisabledKey`. Controls read it to ignore input, and styles read it
through `is_enabled` to dim themselves.

## Rules

- There is no cascade: a value comes from the nearest ancestor that set it, or the key's default.
- Keys are identified by stable names, never types.
- Theme colors resolve at the point of use.
- Built-in styles are expressible with public API alone.

## Edge cases

- **Lookups are linear** in the number of values set above a view. That's fine for a few dozen;
  a flat map snapshot would be the fix if it ever shows in profiles.
- **Changing the theme** re-runs every color effect in the window: correct, but not free. Theme
  switches are rare.
