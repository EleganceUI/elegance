---
title: Lowering
description: "How a View, a cheap description, becomes the Elements that lay out, draw and handle input, and why the two are separate."
sidebar:
  order: 1
---

**Lowering** turns a *description* of UI (a `View`) into the *live objects* that actually lay out,
draw and handle input (`Element`s). It happens once, when a view is mounted. The name comes from
compilers, where a high-level form is "lowered" into a simpler, more concrete one.

## Two layers

| | `View` (what you write) | `Element` (what runs) |
|---|---|---|
| Is | An immutable description: one `shared_ptr` to a node | A retained object in a tree |
| Holds | "a Text saying X, padded 8, with a red background" | A frame, cached sizes, layout state, an event handler, focus state |
| Cost to create | Nearly free; copying bumps a reference count | Real work: fonts loaded, text shaped, effects created |
| Lifetime | Stored, copied, reused, thrown away freely | Lives while it's on screen |

Writing

```cpp
Text("Hi").padding(8).background(Color::red)
```

builds a small chain of nodes and nothing else. Each modifier wraps the previous node:

```
ModifiedNode(background: red)
 └─ ModifiedNode(padding: 8)
     └─ TextNode("Hi")
```

No element exists yet. That's what makes views cheap to pass around, keep in variables, build in
loops and capture in lambdas.

## What lowering does

When the view is mounted (as a window's root, an `If` branch turning on, a `ForEach` item
appearing), `lower(view, context)` walks the chain and builds elements:

```
DecorationElement            ← from .background(red): the content plus a red shape behind it
 ├─ LayoutModifierElement    ← from .padding(8)
 │   └─ TextElement "Hi"     ← from Text
 └─ ShapeElement (red)       ← the background's own view, lowered too
```

Along the way it does the work that only makes sense at mount time:

- **Runs bodies.** A component's `body()` and a `ViewModifier`'s `body(content)` run here, once,
  and their results are lowered in place. This is where "a body runs once" comes from.
- **Applies modifiers by protocol.** A layout modifier becomes a wrapper element. A decoration
  becomes an element holding the content and the secondary view. An environment modifier creates
  **no** element; it changes the `LowerContext` passed down, so `.font(...)` affects everything
  below it. Effect and event modifiers become wrapper elements of their own.
- **Binds reactive arguments.** For `Text([=] { return name(); })`, lowering creates the
  `TextElement` **and one effect** that re-reads `name()` and updates that element's text when it
  changes. A static value is just set once. This is where surgical updates come from.
- **Resolves the environment.** The context carries the current fonts, colors, theme and styles,
  so a `ThemeColor` or a button style is looked up for exactly the place the view is mounted.
- **Assigns ownership.** Effects created while lowering are adopted by the current `Scope`. When
  the subtree is unmounted, the scope is destroyed and all its effects stop.

## Why the split

- **Authoring stays flexible.** Views are plain values with no lifecycle to manage. Nothing costs
  anything until mount.
- **Updates stay surgical.** After lowering, nothing re-runs the description. A state change
  updates one property of one element through its effect. There's no "rebuild the tree and diff
  it", which is what React does, and what SwiftUI does on every body re-evaluation.
- **Order is meaningful.** Each modifier wraps the previous result, so `.padding().background()`
  and `.background().padding()` lower to different trees, matching what they look like.

## When it happens again

Lowering is **not** repeated for the same view on every change. It runs again only when the
structure changes:

- An `If` switches branches: the old branch's elements and scope are destroyed and the new branch
  is lowered.
- A `ForEach` gets new keys: only the new or changed items are lowered.
- A lazy stack scrolls a row into range: that row is lowered.
- A menu or popover opens: its builder runs and its content is lowered in a fresh scope.
- Hot reload swaps a module: its views are lowered again from scratch.

Everything else is effects updating existing elements.

## The same pattern, all the way down

Lowering is the first of several steps, each trading flexibility for speed:

```
View  ──lower (once)──►  Element  ──paint──►  DisplayList  ──plan──►  GPU instances
values you write         live tree           draw commands          one instanced draw
```

Elements **paint** into a display list, a lower-level description in points. The render thread
**plans** that into pixel-space GPU instances. See [A frame, start to finish](../frame/).

## Where to read more

- Spec: [Views, modifiers, and the element tree](../../framework/views-and-elements/)
- Spec: [Reactive core](../../framework/reactive/) (effects and scopes)
- Spec: [If and ForEach](../../framework/dynamic-content/) (lowering again on structural change)
