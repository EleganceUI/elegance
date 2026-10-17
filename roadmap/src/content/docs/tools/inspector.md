---
title: Inspector
description: An F12 window showing the element tree, details, pick mode, and frame stats.
sidebar:
  order: 1
---

A separate window (never an overlay, so it can't change the layout it inspects) that shows any
window's element tree. Build it early: it pays for itself while debugging layout.

:::note[Depends on]
[Scrolling and lists](../../controls/scrolling-and-lists/) and multi-window support in `App`.
:::

## Read first

- `design.md` §16, M5 "Inspector"
- `src/view/inspector.*`, `src/view/inspect.cpp`, `Element::inspect` / `Inspection`

## Tasks

- [ ] F12 in any window opens an Inspector window for it; F12 again, or closing either window, closes it
- [ ] `virtual void Element::inspect(Inspection&)`: name, one-line detail, property list. The default names the element after its type
- [ ] Tree of every element (depth first through `for_each_child`): name, summary (text, radius, child count, effects like `clip_shape(Path)`, `opacity`, `shadow`, `offset`), frame size, collapsible rows
- [ ] Refresh at most twice a second while following the window, plus a Refresh button
- [ ] Keep **index paths**, never element pointers, so the inspector can't outlive what it shows
- [ ] Selecting a row highlights the element in its window
- [ ] Details: frame, last proposal, child count, and properties (font size, colors, fills, strokes, line count)
- [ ] Pick mode: hover outlines the element under the pointer, click selects it (expanding and scrolling the tree to it), Esc cancels
- [ ] Frame stats: UI-thread build time, view count, display-list commands, glyphs and paths; offscreen passes (layers) show as a warning

## Done when

- [ ] Picking any element in the sandbox selects the right row
- [ ] Closing the inspected window while the inspector is open doesn't crash

## Gotchas

- Follow-ups: per-element environment values, modifier fusion display, per-pass GPU timings, and
  **state inspection** (tag each effect with the element being built, give `State<T>` a formatter
  and `source_location`, then list the states and computeds a selection reads, with live editing).
