---
title: ScrollView, lazy stacks, and List
description: Scrolling with eased wheels and scroll bars, virtualized stacks, scroll anchoring, and List.
sidebar:
  order: 2
---

Scroll views clip and offset their content. Lazy stacks build only the rows near the visible
part. Getting them fast and stable is mostly about measuring sizes cheaply and keeping the anchor
still.

:::note[Depends on]
[Basic controls](../basic-controls/) and [If and ForEach](../../framework/dynamic-content/).
:::

## Read first

- `design.md` M4 ScrollView, LazyVStack, List, ScrollPosition, and the 10,000-row fixes; §15 lazy stack notes
- `ScrollElement` in `src/view/element.h`, `src/view/scroll.cpp`, `src/view/lazy_stack.cpp`

## Tasks

### ScrollView
- [ ] `ScrollView(content)` / `ScrollView({.axes, .shows_indicators, .position}, content)`
- [ ] Takes all offered space; content gets its ideal size along scroll axes and the offered size across them (centered there); clipped
- [ ] Wheel notches ease; trackpad (fractional, `precise`) deltas apply directly
- [ ] A view at its end passes the scroll to an outer scroll view
- [ ] Keys unhandled by the focused view scroll it: arrows, Page Up/Down, Home/End
- [ ] `Element::reveal(rect)`: Tab scrolls the newly focused view into sight
- [ ] Scroll bars appear while scrolling or hovered, fade after a pause, and can be dragged
- [ ] Set `DisplayList::cull` to the visible part so off-screen elements aren't painted

### Lazy stacks
- [ ] `LazyVStack` / `LazyHStack({.alignment, .spacing}, ForEach(...))`
- [ ] Build only items in the viewport plus half a viewport each way (`visible_rect()`); destroy items scrolled out of reach, with their scopes
- [ ] Remember measured sizes **by item position, with a running total in `double`**, so measuring is O(1) and arranging looks up only visible rows
- [ ] Unbuilt items are estimated at the average measured size; the total re-settles over a frame
- [ ] When ForEach items change, drop built items whose key is gone or value changed (rebuilt if still in view)
- [ ] Scroll anchoring: when rows above the top visible row change size, shift the scroll offset by the same amount (`anchor_shift`) so nothing on screen jumps
- [ ] Revalidate the dispatcher right after layout (layout built or destroyed elements)

### List and scroll position
- [ ] `List(ForEach(...))` / `List({.position}, ...)`: a lazy stack in a ScrollView on a rounded surface, with row padding, inset separators and hover highlight
- [ ] `ScrollPosition`: `scroll_to_top()`, `scroll_to_end()`, `scroll_to(id)` (a lazy item's key), applied in the next layout
- [ ] `scroll_to(id)` re-aims for a few layouts while estimates become real sizes

## Done when

- [ ] A 10,000-row List scrolls smoothly; the frame cost is about 1.6 ms in debug (the design's measurement after the fix)
- [ ] Scrolling to the end of a list of variable-height rows lands exactly on the last row
- [ ] Nested scroll views hand off the wheel at their ends

## Gotchas

- **Summing sizes in `float` drifted by a whole row over 10,000 items.** Use `double`.
- Before the position-keyed cache, the 10,000-row List took about 16 ms per frame in debug.
- Hover has to be re-evaluated after each frame, because content moves under a still pointer.
- Not built yet: insertion/removal transitions for lazy items, keeping a focused item built while
  it's scrolled away, list selection, sections and headers, swipe actions, `LazyVGrid` /
  `LazyHGrid`, overscroll bounce, wheel momentum, and `ScrollViewReader`.
