---
title: Environment, themes, and styles
description: Inherited values, light and dark themes, ThemeColor tokens, and control style protocols.
sidebar:
  order: 5
---

There are no stylesheets. Reuse comes from environment values (inherited down the tree), custom
modifiers, and per-control style protocols that separate behavior from appearance.

:::note[Depends on]
[Shapes, decorations, and effects](../shapes-and-effects/). Text arrives in part 4. Build
`FontKey` now and wire it up when text exists.
:::

## Read first

- `design.md` §8 (Styling), M3
- `include/elg/environment.h`, `include/elg/theme.h`, `include/elg/styles.h`, `include/elg/modifiers/environment.h`
- `src/view/styles.cpp`

## Tasks

### Environment values
- [ ] Typed keys: a struct with `Value`, a stable `name` string (not `typeid`), and `default_value(const EnvironmentValues&)`
- [ ] `EnvironmentValues` as a persistent list: setting a key prepends, so copies are cheap and earlier copies are unaffected
- [ ] Every value is a reactive argument; reading is tracked
- [ ] Environment-dependent values resolve against the environment they were set in, so a key's value never depends on itself
- [ ] `Environment<Key>()` reads the environment where the code runs (component bodies, ViewModifier bodies, style `make_body`)
- [ ] `.environment<Key>(value)`
- [ ] Built-in keys: font, foreground, tint, color scheme, theme, disabled, button style, toggle style, text field style, hover delay

### Themes
- [ ] `Theme` with light and dark `ColorTokens`; `ThemeColor` tokens (`primary`, `secondary`, `accent`, `background`, `outline`, `danger`, ...)
- [ ] `ThemeColor` works anywhere a color does and resolves where it's used
- [ ] Windows start from `system_prefers_dark()` as reactive state; a system theme change updates only what depends on it
- [ ] `.color_scheme(...)` and `.theme(...)` overrides
- [ ] Closures taking `const EnvironmentValues&` as `Prop`s, to mix state with theme colors

### Styles
- [ ] `ButtonStyle::make_body(ButtonConfiguration)`, where the configuration has the label plus `is_pressed`, `is_hovered`, `is_focused`, `is_enabled`
- [ ] Built-ins using only the public API: `Prominent` (default), `Bordered`, `Plain`
- [ ] `.button_style(...)` applies to every Button in the subtree
- [ ] `.disabled(Prop<bool>)`: controls inside don't respond and look disabled
- [ ] The same protocol shape for `ToggleStyle` and `TextFieldStyle` (built with their controls)

## Done when

- [ ] Switching Windows between light and dark mode recolors a running app without re-running bodies
- [ ] `.color_scheme(dark)` on one subtree makes only that subtree dark
- [ ] A custom `PillButtonStyle` from `design.md` §8 works on every Button inside a stack

## Gotchas

- Where the environment is read matters. A plain function returning a `View` runs *before*
  placement and sees its caller's environment. A component struct's `body()` sees where it's placed.
- Open: theme fonts, spacing and radii tokens; theme token files (JSON/TOML) that reload without a
  rebuild; style protocols for Slider and ProgressView.
