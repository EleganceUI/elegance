---
title: Thinking in Elegance
description: "The model behind the API: views as values, modifiers, fine-grained reactivity, the environment, and the threads."
---

A few ideas explain almost everything in Elegance. If you know SwiftUI, most will feel familiar;
the differences are called out.

## Views are descriptions

A `View` is an immutable, cheaply copyable description of UI. Calling `Text("Hi")` creates nothing
on screen. Elegance builds the real, retained elements when the view is *mounted* in a window,
and keeps them until it's removed.

Because views are values, you can store them, pass them around and reuse them:

```cpp
const View badge = Text("PRO").padding(4).background(ThemeColor::accent, Capsule());
HStack(Text("Elegance"), badge, Spacer(), badge.opacity(0.5f));
```

A *component* is any type with a `View body() const`. It's used like a view:

```cpp
struct SettingRow {
    std::string title;
    std::string detail;

    View body() const {
        return HStack(Text(title), Spacer(), Text(detail).foreground_style(ThemeColor::secondary));
    }
};

VStack(SettingRow{"Plan", "Pro"}, SettingRow{"Storage", "12 GB"});
```

A plain function returning `View` works just as well. The difference is *when* it runs: a
function runs when you call it, while a component's body runs when it's placed, so it can read the
[environment](../../guides/environment/) where it ends up.

## Modifiers wrap views

`view.padding(8)` returns a new view: the old one wrapped in a padding modifier. Order matters,
just as in SwiftUI:

```cpp
Text("A").padding(8).background(Color::hex(0x7AA2F7)); // the background includes the padding
Text("B").background(Color::hex(0x7AA2F7)).padding(8); // the padding is outside the background
```

Every modifier method is sugar for `view.modifier(SomeModifier(...))`, and each modifier
implements one of six protocols: view (composition), layout, decoration, environment, effect or
event. Your own modifiers use the same protocols. See
[Components and modifiers](../../guides/custom-views/).

## State is fine-grained

Elegance has no "re-render the body" step. Instead, arguments can be **reactive**: a closure (or a
`State`, `Binding` or `Computed`) where a value is expected.

```cpp
const State<bool> selected(false);

Text("Item")
    .foreground_style([=] { return selected() ? Color::white : Color::hex(0x1C1C1E); })
    .background([=] { return selected() ? Color::hex(0x0A84FF) : Color::clear; })
    .on_tap_gesture([=] { selected = !selected(); });
```

Each reactive argument becomes one small effect that updates one property of one element. When
`selected` changes, exactly those two effects run; the text isn't measured again, and nothing else
is touched. Updates are batched: effects run once per frame, after the current event.

Structure changes with state through `If` and `ForEach`, which add and remove views as their
inputs change.

## The environment flows down

Some values are inherited by everything inside a view: the font, the foreground color, the tint,
light or dark mode, the theme, whether controls are disabled, and control styles. Set them with
modifiers, high up:

```cpp
VStack(Text("Title"), Text("Body"), Button("OK", ok))
    .font(Font::callout)
    .tint(Color::hex(0x7C3AED));
```

You can define your own environment values. See [The environment](../../guides/environment/).

## Layout is negotiated

Layout works as in SwiftUI: a parent *proposes* a size, the child picks its own size, and the
parent places it. Text takes what it needs and wraps; shapes fill what they're offered; stacks
divide their space among their children; `.frame` constrains. See [Layout](../../guides/layout/).

## Threads

Your code runs on the **UI thread**: building views, event handlers, effects, and callbacks from
Elegance (HTTP results, file dialogs, notifications). State may only be read and written there; doing so
from another thread throws `elg::UsageError`.

A separate **render thread** turns each frame into GPU commands, so a slow frame never blocks
input, and resizing a window stays smooth. Video and camera frames go straight to the render
thread without touching the UI thread.

## Errors

Mistakes in using the API throw `elg::UsageError` with a message saying what was wrong (an
unknown icon name, a malformed SVG path). Resources that fail to load throw `elg::ResourceError`.
Exceptions from your callbacks propagate out of `app.run()` after a clean shutdown. Bugs inside
Elegance itself produce a crash report instead. See [Debugging and errors](../../guides/debugging/).
