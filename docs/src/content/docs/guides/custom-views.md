---
title: Components and modifiers
description: "Building reusable views - functions, components, view modifiers, dot-syntax extensions, and the lower-level modifier protocols."
---

## Functions

The simplest reusable view is a function:

```cpp
View Badge(std::string text, Color color) {
    return Text(std::move(text))
        .font(Font::caption.bold())
        .foreground_style(Color::white)
        .padding(EdgeInsets::symmetric(8, 3))
        .background(color, Capsule());
}
```

State created inside lives as long as the views built there:

```cpp
View ExpandableSection(std::string title, View content) {
    const State<bool> open(false);
    return VStack({.alignment = HorizontalAlignment::leading},
                  Button(Label(std::move(title), {.system_name = "caret-right"}),
                         [=] { with_animation(Animation::smooth(), [&] { open = !open(); }); })
                      .button_style(PlainButtonStyle{}),
                  If(open, std::move(content)));
}
```

## Components

A component is a type with `View body() const`. Any such type converts to a `View`:

```cpp
struct ContactRow {
    Contact contact;

    View body() const {
        return HStack(AsyncImage(contact.avatar_url, {.content_mode = ContentMode::fill})
                          .frame(36, 36)
                          .clip_shape(Circle()),
                      Text(contact.name),
                      Spacer());
    }
};

ScrollView(LazyVStack(ForEach(contacts, &Contact::id, [](const Contact& c) { return ContactRow{c}; })));
```

A component's body runs when the view is **placed** in the tree, not when it's created. That
lets it read the environment where it ends up with `Environment<Key>()`, which a plain function
can't do (it runs before it's placed, so it sees its caller's environment). See
[The environment](../environment/).

## View modifiers

A `ViewModifier` packages a set of modifiers under a name:

```cpp
class Card : public ViewModifier {
public:
    explicit Card(float radius = 16) : radius_(radius) {}

    View body(View content) const override {
        return content.padding(20)
            .background(ThemeColor::surface, RoundedRectangle(radius_))
            .shadow({.color = Color::black.opacity(0.12f), .radius = 12, .y = 4});
    }

private:
    float radius_;
};

Text("Inside a card").modifier(Card());
Text("Rounder").modifier(Card(24));
```

`body` runs once, when the view is mounted, and can read the environment.

## Dot-syntax extensions

To write `.card()` instead of `.modifier(Card())`, add member functions to `View` itself. Put the
declarations in a header, which Elegance includes **inside** the `View` class:

```cpp title="view_extensions.h"
View card(float corner_radius = 16) const;
View badge_overlay(int count) const;
```

```cpp title="view_extensions.cpp"
#include <elg/elegance.h>

elg::View elg::View::card(float corner_radius) const {
    return modifier(Card(corner_radius));
}
```

Then register the header for your target in CMake:

```cmake
elegance_view_extensions(my_app view_extensions.h)
```

Every source file of the target now sees the extra members:

```cpp
Text("Hello").padding(8).card();
```

Extensions may add member functions only. Data members or virtual functions would change `View`'s
layout, and are rejected at compile time.

## The modifier protocols

Under the sugar, every modifier implements one of six protocols. `ViewModifier` is the one you'll
use most; the others hook into specific parts of the engine:

| Protocol | Override | Use |
| --- | --- | --- |
| `ViewModifier` | `View body(View content)` | Composition of other modifiers |
| `LayoutModifier` | `size_that_fits`, `place` | Taking part in layout, around one child |
| `DecorationModifier` | `decoration`, `placement`, `alignment` | A view behind or in front, sized to the content |
| `EnvironmentModifier` | `apply(EnvironmentValues&)` | Changing inherited values |
| `EffectModifier` | `apply(Effects&)` | Clip, opacity, shadow, offset, hit testing |
| `EventModifier` | `make_handler()` | Handling input |

For example, a layout modifier that gives its content a minimum size:

```cpp
class MinSize : public LayoutModifier {
public:
    MinSize(float width, float height) : width_(width), height_(height) {}

    Size size_that_fits(ProposedSize proposal, const LayoutChild& content) const override {
        const Size size = content.size_that_fits(proposal);
        return {std::max(size.width, width_), std::max(size.height, height_)};
    }

    void place(Rect bounds, ProposedSize proposal, const LayoutChild& content) const override {
        const Size size = content.size_that_fits(proposal);
        content.place(aligned_position(bounds, size, Alignment::center), proposal);
    }

private:
    float width_;
    float height_;
};

Text("OK").modifier(MinSize(44, 44));
```

The built-in modifiers are written exactly this way; see the
[modifier reference](../../reference/modifier/).
