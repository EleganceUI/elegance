---
title: Layout
description: SwiftUI's propose / choose / place protocol, stacks, frames, and custom layouts.
sidebar:
  order: 3
---

The parent proposes a size, the child chooses its own, and the parent places it. Each axis of a
proposal is optional: `nullopt` asks for the ideal size, `0` for the minimum, and `∞` for the
maximum.

:::note[Depends on]
[Views, modifiers, and the element tree](../views-and-elements/).
:::

## Read first

- `design.md` §7 (Layout: the SwiftUI model, implementation notes), M5 "Custom Layout, completed"
- `include/elg/layout.h`, `src/view/stack_layout.cpp`, `include/elg/modifiers/layout.h`
- WWDC "Compose custom layouts with SwiftUI"; `apps/sandbox` `FlowLayout`

## Tasks

### Protocol
- [ ] `Layout` with `size_that_fits(proposal, children)` and `place_children(bounds, proposal, children)`, plus `spacer_axis()`
- [ ] `LayoutChild`: `size_that_fits`, `place(point, proposal)`, `layout_priority`, `is_spacer`, `value<Key>()`
- [ ] `Layout::operator()(children...)` with deducing `this`, so a custom layout is used like a stack
- [ ] `LayoutElement` holds the layout, its children, and a `LayoutCache`

### Stacks
- [ ] `VStack` / `HStack` with alignment and spacing (default 8, none next to a `Spacer`)
- [ ] Distribution: higher priority first; within a priority, non-spacers before spacers; then least flexible (`max − min`) first, each offered an equal share of what's left; lower priorities keep their minimums reserved
- [ ] Spacers split only what the content leaves, minus their own minimum lengths
- [ ] `ZStack` with alignment. With mixed priorities, the highest-priority children size the stack, and the others are offered that size
- [ ] `.z_index(Prop<float>)` for stacking order among siblings in any stack (ties keep declaration order)
- [ ] `Spacer(min_length)` and `EmptyView()` (takes no space and no spacing)
- [ ] `aligned_position(container, size, alignment)`

### Layout modifiers
- [ ] `padding` (all, or `Insets`)
- [ ] `frame(width, height, alignment)` and the flexible `frame({.min_width, .ideal_width, .max_width, ...})`
- [ ] `fixed_size(horizontal, vertical)`, `layout_priority(n)`
- [ ] `aspect_ratio(ratio?, ContentMode::fit/fill)`. Without a ratio, use the content's ideal size
- [ ] `.offset(x, y)` is an *effect* (drawing and hit testing only, not layout)

### Custom layouts with a cache
- [ ] `CachedLayout<Cache>`: `make_cache`, `update_cache`, and `size_that_fits` / `place_children` taking the cache
- [ ] The cache lives in the element, is made on first layout, and is marked stale when children change or any descendant invalidates layout
- [ ] Called without framework storage (by another layout), build a temporary cache
- [ ] Layout values: `view.layout_value<Key>(v)` through a layout-transparent `LayoutValueModifier<Key>`, found through wrapping modifiers but never inside nested layouts

## Done when

- [ ] `HStack(Text("long title"), Spacer())` shows the text in full whenever it fits
- [ ] A shape behind `Text(...).layout_priority(1)` in a `ZStack` takes the text's size
- [ ] A sandbox `FlowLayout` wraps tags and honors a `RowBreak` layout value
- [ ] Changing one text in a 1,000-element window re-arranges only its ancestors (count `arrange` calls)

## Gotchas

- Text competes for space like any flexible view: next to a `Spacer` it gets a share and may
  truncate. `.fixed_size(false, true)` makes it take its full height.
- Not built yet: `Grid` / `GridRow`, `LazyVGrid` / `LazyHGrid`, `ViewThatFits`,
  `GeometryReader`, alignment guides, `.position`, and pixel snapping.
