---
title: The environment
description: "Values inherited down the view tree - the built-in ones, defining your own keys, and reading them."
---

The environment holds values that flow **down** the view tree. A modifier sets a value for a view
and everything inside it; views read the nearest value above them.

## Built-in values

| Modifier | Key | Default |
| --- | --- | --- |
| `.font(...)` | `FontKey` | `Font::body` |
| `.foreground_style(...)` | `ForegroundKey` | the theme's `primary` |
| `.tint(...)` | `TintKey` | the theme's `accent` |
| `.color_scheme(...)` | `ColorSchemeKey` | the system's setting |
| `.theme(...)` | `ThemeKey` | `Theme::standard()` |
| `.disabled(...)` | `DisabledKey` | `false` |
| `.button_style(...)`, `.toggle_style(...)`, `.text_field_style(...)` | `ButtonStyleKey`, ... | the default styles |
| `.hover_delay(...)` | `HoverDelayKey` | enter 0.1 s, exit 0 |
| (set by the window) | `WindowKey` | the window the view is in |

Values can be reactive: set one from a state, and only the views reading that key update when it
changes.

```cpp
Root().tint([=] { return accent_color(); });
```

## Your own keys

A key is a type with a `Value` type, a unique `name` and a `default_value`:

```cpp
struct CompactKey {
    using Value = bool;
    static constexpr std::string_view name = "myapp.compact"; // stable and unique
    static bool default_value(const EnvironmentValues&) { return false; }
};
```

Set it with `.environment<Key>(value)`:

```cpp
Settings().environment<CompactKey>(true);
Settings().environment<CompactKey>(compact_state); // reactive
```

## Reading values

`Environment<Key>()` reads a value while views are being built, where the view is placed: in a
component's `body()`, a `ViewModifier`'s `body`, a style's `make_body`, or a window's content
function. It returns a reader; calling it is a reactive read, so pass it inside closures:

```cpp
struct SettingRow {
    std::string title;

    View body() const {
        const auto compact = Environment<CompactKey>();
        return Text(title).font([=] { return compact() ? Font::footnote : Font::body; });
    }
};
```

A plain function returning a `View` runs **before** its result is placed, so it sees its caller's
environment rather than its own. For values that depend on where a view ends up, use a component,
or a closure taking `const EnvironmentValues&`:

```cpp
Circle().fill([](const EnvironmentValues& env) { return env.get<TintKey>().opacity(0.5f); });
```

`EnvironmentValues::get<Key>()` reads any key, and `color(ThemeColor)` resolves a theme color.
