---
title: Layout
description: "The propose/choose/place protocol, how stacks share space, and how layout modifiers and custom layouts fit in."
sidebar:
  order: 3
depends: [framework/views-and-elements]
---

## What it's for

Layout decides every element's frame. Elegance follows SwiftUI's negotiation rather than
flexbox: it's simpler, it composes predictably, and it's the model the rest of the API assumes.

## How it works

### The protocol

1. **The parent proposes** a size. Each axis is `optional<float>`:
   `nullopt` means "your ideal", `0` means "your minimum", `∞` means "your maximum", and a
   number means "this much space".
2. **The child chooses** its size. The parent must accept it, even if it overflows.
3. **The parent places** the child: it picks the child's top-left from its own bounds and an
   alignment, and passes the proposal again so the child can arrange its own children.

A parent may ask a child several times with different proposals while deciding. That's why
`size_that_fits` is memoized per proposal.

Units are logical points. The renderer applies the display scale.

### What views choose

| View | Chooses |
|---|---|
| Shapes, Canvas, Color | The proposal (10×10 for an unspecified axis) |
| Text | Its wrapped size for the proposed width: one line with an ellipsis at minimum, full height at most |
| Spacer | Its minimum length along the stack's axis, flexible up to the proposal; zero across |
| Stacks | The sum of children along the axis plus gaps; the largest across |
| Image (resizable) | The proposal; otherwise its pixel size / scale |

### Stacks

`HStack` and `VStack` share one algorithm along their axis. Given a proposal with a length
`L` along the axis:

1. **Gaps**: `spacing` between children, defaulting to 8, except **0 next to a Spacer**.
2. **Probe** each child with a proposal of 0 and of ∞ along the axis to get its min and max.
   Flexibility = max − min.
3. **Sort** children: higher `layout_priority` first; within a priority, non-spacers before
   spacers; then least flexible first.
4. **Reserve** every child's minimum. Remaining = L − gaps − all minimums.
5. **Distribute, priority group by priority**. When a group starts, its own reservations return
   to the pool. Non-spacers go first, each offered an equal share of what's left (minus the
   spacers' minimums in the group). Then spacers split the rest. Each child's actual answer is
   subtracted before the next child's share is computed, so leftovers flow to more flexible
   children.
6. Without a proposed length (`nullopt`), every child just gets its ideal.

Placement walks the children in declaration order, aligning each on the cross axis by the
stack's alignment fraction (0, 0.5, 1).

Because spacers come after content, `HStack(Text("…"), Spacer())` shows the text in full
whenever it fits, and the spacer takes what's left.

### ZStack

Children overlap, aligned by the stack's alignment. With equal priorities, every child gets the
proposal and the stack is the union of their sizes. With **mixed** priorities, only the
highest-priority children size the stack; the others are offered that size (a background shape
then matches the text it sits behind). `.z_index` reorders drawing and hit testing among
siblings in any stack, with ties kept in declaration order.

### Layout modifiers

Each is a `LayoutModifier` with `size_that_fits(proposal, child)` and `place(bounds, proposal,
child)`:

- **padding**: shrink the proposal by the insets, grow the answer by them.
- **frame(width, height)**: report the fixed size; propose it to the child; align the child inside.
- **flexible frame (`min`/`ideal`/`max`)**: clamp the proposal into [min, max] (or use the ideal
  when unspecified) and pass that on; report the proposal clamped into [min, max], with a missing
  bound taken from the child. `.frame({.max_width = infinity})` fills the proposal;
  `.frame({.min_width = 100})` is at least 100.
- **fixed_size(h, v)**: replace the proposal with `nullopt` on those axes (the ideal).
- **aspect_ratio(r?, fit/fill)**: fit or fill the proposal at the ratio; without a ratio, use the
  child's ideal size.
- **layout_priority(n)**: changes only the priority the parent stack sees.

`.offset` is not layout: it's an effect, and moves drawing and hit testing only.

### Custom layouts

A `Layout` implements `size_that_fits(proposal, children)` and `place_children(bounds, proposal,
children)`, where each `LayoutChild` can be measured, placed, and asked its priority, whether it's
a spacer, and **layout values**. Custom layouts are called like stacks: `FlowLayout{}(a, b, c)`.

`CachedLayout<Cache>` adds SwiftUI's cache. `make_cache(children)` runs on first layout,
`update_cache` when children change or any descendant invalidates layout, and the cache persists
between measuring and placing and between frames. It must not depend on the proposal. Called by
another layout (without framework storage), a temporary cache is built.

Layout values let a child carry data for its parent layout: `view.layout_value<RowBreak>(true)`
adds a layout-transparent `LayoutValueModifier`, and the layout reads `child.value<RowBreak>()`.
Lookup goes through wrapping modifiers but never into nested layouts.

## Rules

- A parent never overrides a child's chosen size.
- Measuring is pure: same proposal, same answer, until invalidated.
- Layout never reads or writes reactive state; reactive changes arrive as invalidations.

## Edge cases

- **Text next to a Spacer** gets an equal share and may truncate. `.fixed_size(false, true)`
  gives it its full height.
- **Overflow**: children larger than the stack draw outside it; clipping is opt-in.
- **Empty fragments** (an `If` with an empty branch) contribute no child and no gap.
