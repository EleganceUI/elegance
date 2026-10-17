---
title: Views, modifiers, and the element tree
description: "View values, the modifier protocols, lowering a view into elements, and what an element does."
sidebar:
  order: 2
depends: [framework/reactive, renderer/ui-pipeline]
---

## What it's for

App code describes UI as **views**: cheap, immutable values chained with modifiers. The framework
**lowers** those values once into a retained tree of **elements**, which keep layout state,
paint into display lists and receive input. This split keeps the authoring API pleasant (values
compose freely, nothing exists until mounted) while the runtime stays fast (no rebuilding or
diffing).

## How it works

### Views are descriptions

A `View` is one `shared_ptr<const ViewNode>`. Copying is a reference-count bump. Each modifier
wraps the current node in a new `ModifiedNode`, like SwiftUI's `ModifiedContent`, so modifiers
never mutate anything and any view can be stored, reused and captured. Node kinds include
`LayoutNode` (a layout plus children), `TextNode`, `ShapeNode`, `ButtonNode`, `IfNode`,
`ForEachNode`, `CanvasNode`, and `DeferredNode`. The last holds a function that produces a view at
mount time: components with a `body()` lower through it, so their bodies run where they're
placed.

Types are erased on purpose. SwiftUI's nested generic types would explode C++ compile times and
error messages, and since bodies run once, erasure only costs a little at mount.

### Modifiers are protocols

`Modifier` has a closed set of subclasses, one per pipeline stage, dispatched by a
`ModifierProtocol` enum rather than RTTI (`typeid` breaks across hot-reloaded DLLs):

| Protocol | Lowers to | Examples |
|---|---|---|
| `ViewModifier` | Its `body(content)`, lowered in place | User modifiers like `Card` |
| `LayoutModifier` | A `LayoutModifierElement` around the content | padding, frame, fixed_size, aspect_ratio, layout_priority |
| `DecorationModifier` | A `DecorationElement`: content + a secondary view behind or in front | background, overlay, border |
| `EnvironmentModifier` | Nothing; it changes the environment for the content | font, foreground_style, tint, disabled |
| `EffectModifier` | An `EffectElement`; opacity, offsets and z-index bound reactively | opacity, clip_shape, shadow, offset, z_index |
| `EventModifier` | An `EventElement` with the handler it makes | on_tap_gesture, on_hover, on_key_press |

**Every built-in dot-method is sugar**: `.padding(8)` builds a public `PaddingModifier` and
passes it to `View::modifier()`. There's no private path, so anything a built-in does, a user
modifier can do. The built-ins are written against the public protocols (dogfooding).

Order is preserved: `.padding(8).background(c)` wraps padding first, so the background covers the
padding; reversed, it doesn't.

### Lowering

`lower(view, context)` recursively turns nodes into elements. The `LowerContext` carries what
the subtree needs:

- the **environment** (fonts, colors, theme, styles; see [Environment](../environment-and-styles/)),
- the window's **text system**, **focus manager** and **window services**,
- `needs_frame` and `tree_changed` flags to raise,
- the enclosing **implicit animation**, and whether layout animates,
- the **spacer axis** of the parent stack (`nested()` drops it for anything not a direct child).

Reactive arguments are bound during lowering: each becomes one effect, adopted by the current
scope, that applies the new value to one element property and raises `needs_frame`.

`EnvironmentScope` makes `Environment<Key>()` read the environment being lowered while bodies
and styles run.

### Elements

`Element` is the retained node. Its interface:

- **Layout**: `size_that_fits(proposal)` is memoized per proposal (a small list, cleared at 16
  entries) until `invalidate_layout()`. `place(position, proposal)` sizes the element, sets its
  frame and calls `arrange`. Subclasses implement `measure` and `arrange`.
- **Skipping work**: `place` returns at once when the element was placed before, nothing
  invalidated it, nothing inside is moving, and the target frame and proposal are unchanged. A
  small change in a big window therefore re-arranges only the path from the change to the root.
  `invalidate_layout()` walks up clearing caches; `always_arranges()` opts out (scroll views,
  carets).
- **Painting**: `paint(list)` appends commands. After layout, each element knows its
  `paint_bounds_` (its frame plus children, strokes and shadows). Elements outside the list's
  `cull` rect are skipped unless something inside is moving toward view. Every setter that
  changes how an element looks should also call `invalidate_paint()`, so the renderer can redraw
  only what changed (see [Incremental rendering](../../renderer/incremental/)).
- **Frames animate**: the frame is an `Animated<Rect>`. During an animated update a new frame
  eases there (see [Animation](../animation/)).
- **Hit testing**, **event handler**, **focus ring**, **accessibility**, **inspection**: virtual
  hooks covered by later specs.
- **Tree**: `parent()`, `adopt(child)`, `for_each_child`, and `collect_layout_children` (how a
  parent layout sees this element: itself, or a fragment's current children, or nothing).

### Extending `View` with dot-syntax

C++ has no extension methods, so a user header is `#include`d **inside** `class View` when
`ELG_VIEW_EXTENSIONS` is defined, the technique Eigen and Dear ImGui use. It may only declare
non-virtual member functions; `static_assert`s check that `View` stays one pointer and not
polymorphic. The functions are defined in app code and come down to `.modifier()` calls.
`elegence_view_extensions(target header)` sets the define per target. `Text` and `Shape` have
matching hooks.

### Mounting a window

`app.window(desc, content)` registers a window. At `run()`, each window gets a scope, a dirty
flag, focus manager and services, and its root view is lowered inside its scope. Each frame,
`build_frame` proposes the window's logical size, places the root centered (as SwiftUI does), and
paints.

## Rules

- Views never change after creation; elements never reach back into views.
- One entry point for modifiers; built-ins use only public protocols.
- No `typeid` or addresses as identity.
- Destroy a scope before the elements its effects write to.

## Edge cases

- **Type-specific modifiers** (`Text::bold()`, `Shape::fill()`) return their specific type; a
  generic modifier returns plain `View`, so specific ones must come first.
- **A plain function returning a View** runs before placement, so `Environment<K>()` inside it
  sees the caller's environment. A component struct's body sees where it's placed.
- **Extension headers technically violate ODR** (the library sees `View` without the extra
  members). It's safe on MSVC, GCC and Clang because layout and vtable are identical; the
  static_asserts guard what would break.
