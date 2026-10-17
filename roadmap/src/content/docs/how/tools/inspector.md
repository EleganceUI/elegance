---
title: Inspector
description: "A separate window that shows another window's element tree, selection highlights, pick mode, and frame cost."
sidebar:
  order: 1
depends: [controls/scrolling-and-lists]
---

## What it's for

When layout surprises you, you need to see what the framework built: which elements exist, their
frames, the proposals they got, and what each frame cost. The inspector shows that live for any
window, without changing what it inspects.

## How it works

### A separate window

F12 in a window opens an **Inspector window** for it. F12 again, or closing either window, closes
it. One window is inspected at a time. It's a separate window rather than an overlay, so the
inspected layout is never disturbed. The inspector is built with Elegence's own views (a List of
rows, a details panel, buttons), which also makes it a large dogfooding test.

### Paths, not pointers

The inspected tree can change at any moment (If/ForEach, lazy stacks), and the inspector must
never hold a pointer to an element that may be gone. So every element is identified by its
**path**: the child indices from the root, in `for_each_child` order. Selection, expansion and
picking all store paths. Following a path re-walks the live tree and simply fails if the tree no
longer has that element.

### What's shown

**Rows**: the App flattens the tree depth first into rows of path, depth, name, summary and frame,
and refreshes them **at most twice a second** while following the window (plus a Refresh
button). Each element describes itself through `virtual void inspect(Inspection&) const`:

- `name`: defaults to the element's type, overridden where a better name exists ("VStack",
  "Text");
- `detail`: a one-line summary (the text, a shape's radius, a stack's child count, effects such as
  `clip_shape(Path)`, `opacity`, `shadow`, `offset`);
- `properties`: name/value pairs for the details panel (font size, colors, fills, strokes, line
  count).

Rows with children have a chevron and can collapse; the list shows only rows not inside a
collapsed one.

**Details** for the selected row: frame, last proposal (`Element::last_proposal()`), child count
and properties.

**Frame stats** reported by the App after each frame of the inspected window: UI-thread build
time, number of views, display-list commands, glyphs and paths. Offscreen layers are flagged as a
warning, since they're the expensive part.

### Highlights and picking

Selecting a row draws a highlight over that element **in the inspected window**: an overlay
painted after the window's content, so it's never part of the inspected tree.

**Pick mode**: while active, the App routes the inspected window's pointer to the inspector
instead of the app. Hovering outlines the element under the pointer (by hit-testing the tree);
clicking selects it, which expands the rows around it and scrolls the list to it. Esc cancels.

## Rules

- Never keep element pointers across frames.
- Never alter the inspected window's tree or layout.
- Refresh on a budget, not every frame.

## Edge cases

- **The inspected window closes**: the inspector closes with it.
- **A selected element disappears**: its path no longer resolves, and the selection shows as gone.
- **Planned**: per-element environment values, modifier fusion, GPU timings per pass, and
  inspecting state (which States and Computeds an element reads, with values and live editing).
