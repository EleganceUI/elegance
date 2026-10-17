---
title: ScrollView, lazy stacks, and List
description: "Scroll offsets and clipping, wheel and scroll-bar behavior, virtualized stacks with size estimates, and scroll anchoring."
sidebar:
  order: 2
depends: [controls/basic-controls, framework/dynamic-content]
---

## What it's for

Content larger than its space has to scroll, and long collections must stay fast: a 10,000-row
list can't build 10,000 rows. ScrollView provides the viewport, lazy stacks build only what's near
it, and List puts the two together with a look.

## How it works

### ScrollView layout

`ScrollElement` takes **all the space it's offered** (with no proposal, the content's size). The
content is proposed `nullopt` along the scroll axes (so it takes its ideal length) and the
offered size across them. Along a scroll axis, content starts at the leading edge; across, it's
centered. The content is clipped to the viewport, and the scroll view sets the display list's
`cull` rect while painting it, so off-screen elements skip painting.

The **offset** is clamped to `[0, content − viewport]` after every layout, so a shrinking list
never leaves blank space. When the view is **following the end** (scrolled to the bottom, like
a chat log) and the content grows, the offset moves with it. Scrolling up stops following.

### Input

- **Wheel**: notches (integer deltas from a mouse) **ease** to the target over a short animation.
  Trackpad deltas (`precise`) apply directly, because the OS already smooths them.
- **Chaining**: a scroll view already at its end doesn't consume the wheel, so the event bubbles to
  an outer scroll view.
- **Keys** the focused view didn't handle: arrows scroll a line, Page Up/Down a viewport, Home/End
  to the ends.
- **`reveal(rect)`**: when Tab focuses something out of view, each ancestor scroll view scrolls just
  enough to show it.

### Scroll bars

Bars appear while scrolling or when the pointer is over the view, and fade after a pause unless
hovered or being dragged. Thumb length is viewport / content. Dragging the thumb maps its travel to
the whole scroll range. With both axes scrolling, the bars leave the corner free.

Because content moves under a still pointer, hover is re-evaluated after every frame.

### Lazy stacks

`LazyVStack` / `LazyHStack` take a `ForEach` and build items on demand during layout:

1. **The visible stretch**: ask the enclosing scroll view for `visible_rect()`, convert it to the
   stack's coordinates, and extend it by **half a viewport each way**, so items exist before they
   scroll in.
2. **Positions without building**: sizes of items measured so far are stored **by position**,
   with a **running total in `double`**. Unbuilt items are estimated at the **average** measured
   size. An item's offset is the measured total up to it plus estimates, which is O(1) to measure
   and cheap to arrange.
3. **Build** items whose estimated range overlaps the stretch: mount each in its own scope (as
   If/ForEach do), propose the cross size and `nullopt` along the axis, record the real size.
4. **Destroy** built items now outside the stretch, scope first.
5. **Re-settle**: if real sizes replaced estimates, the total changed, so request another layout
   next frame.

When the ForEach's items change, built items whose key disappeared or whose value changed are
destroyed (and rebuilt if still in view). The rest keep their state.

### Scroll anchoring

When rows **above** the top visible row change size (estimates becoming real, inserts), the
content under the viewport would jump. The stack remembers the first row in view and where it
was. After layout, if that row moved by Δ, it calls `anchor_shift(axis, Δ)` on the scroll view,
which moves the offset (and any running scroll animation) by Δ. Nothing on screen jumps. At the
very top, anchoring is skipped so rows inserted above the first come into view (browsers do the
same).

### Programmatic scrolling

`ScrollPosition` is a handle passed in the options. `scroll_to_top()`, `scroll_to_end()` and
`scroll_to(id)` queue a request applied in the scroll view's next layout. For an id, it asks the
lazy stack for `item_offset(key)`. Because that offset may be an estimate, it **re-aims for a few
layouts** while items above get measured.

### List

`List(ForEach(...))` is a lazy vertical stack inside a ScrollView on a rounded surface. A `ListStyle`
hook wraps each row (inside the row's scope) with padding and a hover highlight, and the stack
draws **inset separators** between rows.

## Rules

- Build only what's near the viewport; destroy what scrolled out of reach, scope first.
- Sums of sizes in `double`.
- Never let content jump under the viewport because of measurements elsewhere.

## Edge cases

- **Float drift**: summing 10,000 sizes in `float` drifted by a whole row. Use `double`.
- **A focused item scrolled away** is destroyed today, and focus is lost. Keeping it built is a
  follow-up.
- **Lazy items don't animate in or out** yet.
- **Nested scroll views** on the same axis rely on chaining at the ends.
