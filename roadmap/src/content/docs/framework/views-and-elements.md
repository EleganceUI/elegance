---
title: Views, modifiers, and the element tree
description: Type-erased View values, the modifier protocols, lowering, and retained elements.
sidebar:
  order: 2
---

A `View` is an immutable description. Mounting **lowers** it into a retained tree of `Element`s,
which lay out, paint into a display list, and handle input. App code never sees elements.

:::note[Depends on]
[Reactive core](../reactive/) and [Display list and the UI ubershader](../../renderer/ui-pipeline/).
:::

## Read first

- `design.md` §7 (Element tree), §9.1–9.4 (Authoring API, modifier protocols, user extensions)
- `include/elg/view.h`, `include/elg/modifier.h`, `include/elg/modifiers/*.h`
- `src/view/view_node.h`, `src/view/view.cpp`, `src/view/modifiers.cpp`, `src/view/element.*`
- `cmake/EleganceViewExtensions.cmake`, `apps/sandbox/view_extensions.*`

## Tasks

### View values
- [ ] `View` holds a `shared_ptr<const ViewNode>`. Copying is cheap, and nothing is created until mount
- [ ] `ViewNode::lower(const LowerContext&) -> unique_ptr<Element>`
- [ ] `LowerContext`: text system, `needs_frame`, `tree_changed`, environment, spacer axis, implicit animation, `animates_layout`, focus manager, window services; `nested()` drops the spacer axis
- [ ] Components: a function returning `View`, or a struct with `View body() const` (lowered through a `DeferredNode`, so the body runs at mount)
- [ ] Option structs take `OptionalLength`, not `optional<float>` (see Gotchas)

### Modifier protocols
- [ ] `Modifier` base with a closed set of subclasses, dispatched by a `ModifierProtocol` enum (no RTTI)
- [ ] `ViewModifier` (`body(content)`), `LayoutModifier` (`size_that_fits`, `place`, optional priority), `DecorationModifier` (secondary view, placement, alignment), `EnvironmentModifier` (`apply(values)`), `EffectModifier` (`apply(Effects&)`), `EventModifier` (`make_handler()`)
- [ ] `View::modifier(M)` is the one entry point; every built-in dot-method is a one-liner that builds a public modifier struct
- [ ] Declare the sugar in `view.h`; define it in the library. Put modifier structs in `elg/modifiers/{layout,decoration,effects,environment,events}.h`

### Lowering
- [ ] `ModifiedNode` lowers by protocol: a view modifier expands its body; the rest wrap the content element in `LayoutModifierElement`, `DecorationElement`, `EffectElement`, or `EventElement`, or change the context's environment
- [ ] Preserve modifier order (`.padding().background()` differs from `.background().padding()`)
- [ ] `EnvironmentScope` makes `Environment<K>()` work while bodies and styles run

### Element base
- [ ] `size_that_fits(proposal)` memoized per proposal (a small cache, cleared at 16 entries)
- [ ] `place(position, proposal)`: skip entirely when nothing changed (same target, same proposal, not invalidated, nothing moving), so a small update in a big window lays out only what it touched
- [ ] `invalidate_layout()` walks up, clearing caches; `invalidate_paint_bounds()` for offsets
- [ ] `invalidate_paint()` marks an element paint-dirty (and its ancestors "dirty inside"). Call it from **every** paint-affecting setter as you write each element, even before anything uses it: see [Incremental rendering](../../renderer/incremental/), phase 0
- [ ] `paint(list)`: cull when `paint_bounds_` doesn't overlap `list.cull`; draw the focus ring
- [ ] Virtual hooks: `measure`, `arrange`, `paint_impl`, `hit_impl`, `for_each_child`, `collect_layout_children`, `always_arranges`, `paint_extent`, `cullable`, `inspect`, `accessibility`
- [ ] `parent()`, and `adopt(child)` on construction

### User extensions
- [ ] `#include ELG_VIEW_EXTENSIONS` inside `class View` (and `ELG_TEXT_EXTENSIONS` / `ELG_SHAPE_EXTENSIONS`)
- [ ] `static_assert`s: `sizeof(View) == sizeof(ViewRef)` and `!is_polymorphic_v<View>`
- [ ] `elegance_view_extensions(target header)` sets the define per target
- [ ] `.apply(fn)` for function-style modifiers

### Window mount
- [ ] `App`, `app.window(desc, content)`, `app.run()`: per window, a `Scope`, a dirty flag, then lower the root
- [ ] `build_frame`: propose the window's logical size, place the root centered (as SwiftUI does), paint, post

## Done when

- [ ] `Text("Hi").padding().background(Color::red)` shows a padded red box
- [ ] A user `ViewModifier` (`Card`) and an extension method (`.card()`) both work from app code with the framework built as a DLL
- [ ] Changing one reactive color repaints without re-running any `body()`

## Gotchas

- `std::optional<float>` in option structs triggers narrowing warnings at `/W4` for int literals
  (`{.spacing = 12}`). Make your own `OptionalLength` that accepts any number.
- Type-specific modifiers (`Text::bold()`, `Shape::fill()`) return the specific type, so they
  must come **before** generic ones, as in SwiftUI.
- Member declaration order matters wherever a `Scope` and an element live together: destroy the
  scope first, so no effect runs against a destroyed element.
- The ODR "violation" of extension headers is deliberate; Eigen and ImGui rely on the same trick.
  The static_asserts guard the parts that would actually break.
