---
title: Accessibility
description: "How Elegance apps work with screen readers, and the modifiers that improve what they say."
---

Elegance exposes every window's UI to assistive technology through
[AccessKit](https://accesskit.dev). On Windows, that's UI Automation, used by Narrator, NVDA and
JAWS, as well as by testing tools.

## What you get for free

Built-in views describe themselves:

- `Text` is a label with its text.
- `Button` is a button named by its label's text, and screen readers can press it.
- `Toggle` is a check box or switch with its state, and can be toggled.
- `Slider` and `NumberInput` expose their value and range, and can be set, incremented and
  decremented.
- `TextField` and `TextArea` expose and accept their value; a secure `TextField` is marked as a password.
- `Progress` (with its fraction, when it has one) and images have their roles. A `Separator`
  reads as a separator, which isn't focusable; add `.accessibility_hidden()` to one that's only
  decoration.
- Keyboard focus is reported, so screen readers follow Tab.

Accessibility is on by default (the `ELG_ACCESSIBILITY` CMake option). The tree is only built
when an assistive technology asks for it, so it costs nothing otherwise.

## Improving what's said

Some views need help. An icon-only button says nothing, because a glyph has no words:

```cpp
Button(Image("trash"), remove).accessibility_label("Delete");
```

The modifiers:

```cpp
.accessibility_label("Volume")                  // what it's called
.accessibility_value([=] { return std::format("{}%", volume()); }) // what it holds, in words
.accessibility_hint("Deletes the draft")        // what using it does, if the label doesn't say
.accessibility_hidden()                         // decoration: leave it (and its contents) out
.accessibility_role(AccessibilityRole::heading) // what it is
.accessibility_element()                        // one element for everything inside
.accessibility_action([=] { open(); })          // what the default action (click) does
```

Labels, values and hints can be reactive.

### Custom controls

A view made clickable with a gesture is just a shape and some text to a screen reader. Give it a
role, a label and an action:

```cpp
ZStack({.alignment = Alignment::leading}, Capsule().fill(track_color),
       Circle().fill(Color::white).frame(28, 28).padding(2).offset(knob_x, 0.f))
    .frame(52, 32)
    .on_tap_gesture(toggle)
    .accessibility_element()
    .accessibility_role(AccessibilityRole::toggle)
    .accessibility_label("Dark mode")
    .accessibility_value([=] { return dark() ? "On" : "Off"; })
    .accessibility_action(toggle);
```

### Rows

`accessibility_element()` makes a group read as one line, labeled with the text inside, instead of
one element per text:

```cpp
HStack(Image("user"), Text(name), Spacer(), Text(email)).accessibility_element();
```

## Checking your app

Turn on Narrator (Ctrl+Win+Enter) and move through the app with Tab and the Narrator keys.
Accessibility Insights for Windows and the `Inspect` tool from the Windows SDK show the UI
Automation tree directly.

## Not yet supported

Linux (AT-SPI) and macOS (NSAccessibility) adapters, menu and list roles, live-region
announcements, text caret and selection in text fields, and reduced-motion and high-contrast
settings are planned.
