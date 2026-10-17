---
title: Redrawing only what changed
description: "Why a small change should cost a small amount, how damaged regions do it, and why translucent and overlapping views need no special handling."
sidebar:
  order: 3
---

:::caution[Design, not built]
Today a frame that renders repaints and redraws the whole window. This page explains the
approach in [Incremental rendering](../../renderer/incremental/), which is how that should change.
:::

## The problem

Elegence only renders when something changes, and its reactivity and layout touch only what
changed. But once a frame is rendered, painting and drawing cover the **whole window**. In a
large app, a hover highlight, a blinking caret or a small spinner then costs as much as redrawing
every view on screen, at every resolution.

The goal is that **every stage costs about as much as what changed**.

## The idea: damaged regions

Instead of "which elements changed", the renderer works with **where things changed**:

1. Each element that changed contributes its **old and new paint bounds**: where it was drawn, and
   where it will be drawn. Paint bounds include what reaches outside the frame: shadows, strokes,
   focus rings, glyph overhangs.
2. Those rectangles, merged and capped, are the frame's **damage**.
3. The previous frame stays in a persistent image. The GPU redraws **only inside the damage**,
   scissored, leaving everything else as it was.
4. The compositor is told which rectangles changed, so it recomposes only those.

### Everything under the damage is redrawn

Inside a damaged rectangle, the renderer clears to the background and redraws **every view that
overlaps it, in painter's order**, not just the one that changed. That one rule is what keeps the
result correct.

## Translucent and overlapping views

What if the view that changed is underneath a translucent panel, or a view's own opacity
changes?

Nothing needs to tell the views above that something below them changed. They're redrawn
because they **overlap the damaged region**:

- **A change under a translucent overlay**: the damage covers the changed view's area. The
  overlay intersects it, so it's redrawn there in order, and it blends over the freshly drawn
  pixels underneath.
- **A view whose opacity changes**: its own bounds are damaged, so whatever shows through it is
  redrawn underneath before it's drawn again at its new opacity.
- **A background behind the change**: it overlaps too, so it's redrawn first.

Working by region rather than by element is what makes this automatic.

## Where "old and new bounds" isn't enough

Some effects reach beyond the view that changed. The design accounts for them:

- **Shadows and blurs of a group.** A card's shadow is a blur of everything inside it. A change
  to a child changes the shadow up to about 3σ around the child. Damage grows by the blur reach
  (and shadow offset) of every enclosing group that blurs or shadows its contents.
- **Cached group textures.** If an opacity group's rendered texture is cached, any change *inside*
  it must throw the cache away, not just a change to the group.
- **Skipping work by bounds.** When the renderer skips views outside the damage, it must use each
  view's full drawn area (anti-aliasing, shadow spread), or an overlay whose shadow reaches into
  the damage would be skipped.

## Knowing what changed

All of this depends on every change being reported. Each element setter that changes how it
looks (a color, opacity, offset, text, an image) calls `invalidate_paint()`. That's why the
roadmap asks for the call in every setter from the start, long before anything uses it: adding it
to every element later is tedious and easy to get wrong.

A missed invalidation leaves **stale pixels** that look almost right. So the design includes a
debug overlay that flashes each frame's damage, and a validation mode that renders each frame
both fully and incrementally and reports any difference.

## Beyond the GPU

Redrawing less on the GPU is the first win. The UI thread still walks every element to build the
display list, so the next steps are:

- **Retained paint**: each element keeps the commands it painted last time, and a clean subtree
  reuses them.
- **Delta frames**: the render thread keeps the display list, and the UI thread sends only what
  changed.

Then every stage, from the event to the present, costs what the change costs.

→ Spec and plan: [Incremental rendering](../../renderer/incremental/)
→ Background: [A frame, start to finish](../frame/)
