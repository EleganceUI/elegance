---
title: A frame, start to finish
description: "When Elegence renders, what each stage of a frame does, which thread does it, and which stages scale with what changed."
sidebar:
  order: 2
---

This page follows one change, say a button turning red when hovered, from the input event to
the pixels on screen. Every stage gets its own spec; this is how they fit together.

## When a frame happens at all

Elegence renders **only when something changed**. An idle window sits blocked in the OS's event
wait, using no CPU or GPU. Something has to wake it:

- an input event (pointer, key, text, wheel);
- a state change, from an event handler or from `post_to_ui_thread` / `post_ui_task`;
- a timer the app scheduled: a hover delay, a caret blink, a tooltip;
- glyphs finishing on worker threads, or an image finishing decoding;
- an animation in progress, which keeps frames coming until it settles.

Each window has a **dirty** flag. A frame is built for a window only when it's dirty or animating.

## The stages

```
UI thread:      event → effects → layout → paint ──► mailbox
Render thread:                                       mailbox → plan → record → submit → present
```

### 1. Event

The platform layer delivers the pointer move. The window's **event dispatcher** hit-tests the
element tree, works out which handlers the pointer entered and left, and after the button's hover
delay tells its handler. The handler writes `hovered = true`, a `State`.
→ [Hit testing, events, and focus](../../framework/input-and-focus/)

### 2. Effects

Writing the State marks its dependents, and the loop **flushes effects**. Only effects that read
`hovered` run. Here that's the one applying the button style's background color, which calls the
background shape's `set_fill(red)`. No `body()` runs and nothing is rebuilt. The effect marks the
window dirty.
→ [Reactive core](../../framework/reactive/)

### 3. Layout

The App proposes the window's size to the root. `place()` skips every element whose frame,
proposal and contents are unchanged. A color change didn't invalidate layout, so this is almost
free. A text change would re-measure that text and its ancestors, and nothing else.
→ [Layout](../../framework/layout/)

### 4. Paint

The root paints the tree into a new **display list**: draw commands in points and sRGB (rounded
rects, glyphs, images, clips, layers). Elements scrolled out of view are skipped.
→ [Display list and the UI ubershader](../../renderer/ui-pipeline/)

### 5. Handoff

The frame (the display list plus size and scale) is posted to the **mailbox**. If the render
thread hasn't taken the previous frame yet, the new one replaces it: a slow GPU never builds a
backlog. New glyph pixels are posted ahead of it.
→ [Render thread and frame mailbox](../../renderer/render-thread/)

### 6. Plan, record, submit, present (render thread)

The render thread converts the list into GPU instances in pixels, resolving clips and splitting
offscreen layers into their own passes. It records one command buffer, renders the layers and
then the window in mostly one instanced draw per pass, and presents. On Windows the present goes
through DirectComposition.
→ [Windows and DirectComposition](../../controls/windows/)

## Which stages scale with the change

| Stage | Cost for "one button changed color" |
|---|---|
| Event, effects | Only the effects reading the changed state: proportional to the change |
| Layout | Unchanged subtrees are skipped: close to zero for a color change |
| Paint | **The whole window** is walked and painted into a new list |
| Plan, GPU, present | **The whole window** is planned, cleared and redrawn |

The first stages are already surgical. The last ones redraw everything, which is cheap per
frame (glyphs, images and path masks are all cached, and the draw is mostly one call) but grows
with the window's size and the app's size. The design for making those stages proportional too
is [Incremental rendering](../../renderer/incremental/), explained in
[Redrawing only what changed](../redraw/).

## Animations

While anything animates, the dirty flag stays set and the loop renders every frame, paced by
the display (it waits for the previous present). Animated values are evaluated at one **frame
time** per frame. When the last animation settles, the loop goes back to waiting, and CPU drops to
zero.
→ [Animation and transitions](../../framework/animation/)

## Video, the exception

A playing video doesn't go through the UI thread at all. The render thread keeps the window's
last frame, checks the decoder every few milliseconds, and redraws the kept frame with the new
video picture.
→ [Video](../../media/video/)
