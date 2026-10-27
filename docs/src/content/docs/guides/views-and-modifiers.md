---
title: Views and modifiers
description: "Building UI from views, chaining modifiers, and splitting it into reusable pieces."
---

Everything on screen is a `View`. You build a UI by combining small views into bigger ones and
adjusting them with modifiers.

## Built-in views

| Kind | Views |
| --- | --- |
| Containers | `VStack`, `HStack`, `ZStack`, `LazyVStack`, `LazyHStack`, `ScrollView`, `List` |
| Content | `Text`, `Image`, `Label`, `AsyncImage`, `VideoView`, `CameraView`, `Canvas` |
| Shapes | `Rectangle`, `RoundedRectangle`, `Capsule`, `Circle`, `PathShape`, `CustomShape` |
| Controls | `Button`, `Toggle`, `TextField`, `TextArea`, `Slider`, `NumberInput`, `Picker`, `Progress`, `MenuButton` |
| Structure | `If`, `ForEach`, `Spacer`, `Separator`, `EmptyView` |
| Menus | `Menu`, `MenuItem`, `Submenu` |

Each is a function returning a `View` (or a `Text` or `Shape`, which are views with extra
modifiers). Containers take their children as arguments:

```cpp
VStack(Text("Title").font(Font::title),
       HStack(Image("star"), Text("4.8")),
       Button("Buy", buy));
```

Options come first, as a designated initializer:

```cpp
VStack({.alignment = HorizontalAlignment::leading, .spacing = 4}, Text("Name"), Text("Detail"));
```

## Modifiers

A modifier returns a new view that wraps the old one. Chain as many as you like; they apply from
the inside out:

```cpp
Text("Save")
    .font(Font::headline)
    .foreground_style(Color::white)
    .padding(EdgeInsets::symmetric(16, 8))
    .background(ThemeColor::accent, Capsule())
    .shadow({.radius = 6, .y = 2});
```

Because each modifier wraps everything before it, order changes the result. A background applied
after padding covers the padding; one applied before doesn't. A frame applied before a background
sizes the background too.

The modifiers, grouped by what they do:

| Group | Modifiers |
| --- | --- |
| Layout | `padding`, `frame`, `fixed_size`, `aspect_ratio`, `resizable`, `layout_priority`, `z_index`, `offset` |
| Decoration | `background`, `overlay`, `border` |
| Effects | `clipped`, `clip_shape`, `corner_radius`, `opacity`, `shadow`, `content_shape`, `allows_hit_testing` |
| Events | `on_click`, `on_double_click`, `on_activate`, `on_pointer_enter`, `on_pointer_leave`, `on_hover_change`, `on_drop`, `on_key_press`, `focusable`, `focused`, `context_menu`, `help`, `window_drag_area` |
| Environment | `font`, `font_weight`, `foreground_style`, `tint`, `color_scheme`, `theme`, `disabled`, `button_style`, `toggle_style`, `text_field_style`, `hover_delay`, `environment<Key>` |
| Animation | `animation`, `transition`, `animates_layout` |
| Accessibility | `accessibility_label`, `accessibility_value`, `accessibility_hint`, `accessibility_hidden`, `accessibility_role`, `accessibility_element`, `accessibility_action` |

All of them are listed with their exact signatures in the
[`<elg/view.h>` reference](../../reference/view/).

### Text and Shape modifiers

`Text` and `Shape` have modifiers of their own that return the same type, so they chain before
the generic ones:

```cpp
Text("Hello").font(Font::title).bold().line_limit(1).padding(8);   // Text modifiers, then View
Circle().fill(ThemeColor::accent).stroke(Color::white, 2).frame(40, 40);
```

Once a generic modifier is applied, the result is a plain `View`, and `.bold()` or `.fill()` are
no longer available.

## Reactive arguments

Most modifier arguments accept a value, or anything that produces one: a closure, a `State`, a
`Binding`, a `Computed`, or a `ThemeColor`.

```cpp
.opacity(0.5f)                                   // fixed
.opacity(fade)                                   // a State<float>
.opacity([=] { return hovered() ? 1.f : 0.6f; }) // derived from state
.foreground_style(ThemeColor::secondary)         // resolved from the theme where it's used
```

These parameters have type `Prop<T>`. A reactive one becomes a small effect that updates that one
property whenever the state it read changes. See [State and data flow](../state/).

## Reusing views

Split a UI into functions that return views:

```cpp
View Avatar(Color color) {
    return Circle().fill(color).frame(40, 40);
}

View Row(std::string name, Color color) {
    return HStack(Avatar(color), Text(std::move(name)), Spacer());
}
```

Or into components, structs with a `body()`, which also see the
[environment](../environment/) where they're placed:

```cpp
struct Row {
    std::string name;
    View body() const { return HStack(Text(name), Spacer()); }
};
```

Bundle a recurring set of modifiers into a `ViewModifier`, or add your own dot-syntax methods to
`View`. See [Components and modifiers](../custom-views/).

### Applying a function

`apply` passes the view to a function, which keeps long chains readable:

```cpp
const auto card = [](View content) { return content.padding(16).background(ThemeColor::surface, RoundedRectangle(12)); };
Text("Inside a card").apply(card).shadow();
```
