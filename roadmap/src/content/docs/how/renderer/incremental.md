---
title: Incremental rendering
description: "Making every stage cost proportional to what changed: invalidation, damage, partial redraws and presents, retained paint, and delta frames."
sidebar:
  order: 4
depends: [renderer/ui-pipeline, framework/views-and-elements, framework/animation, controls/windows, controls/scrolling-and-lists]
---

:::caution[Design, not built]
Today Elegance redraws the whole window whenever anything changes (see "Where it stands"). This
page is the design for redrawing only what changed.
:::

## What it's for

A large app has thousands of elements on screen. When one button changes color, the work should
be about one button's worth. Otherwise every hover, caret blink and progress spinner costs a
full-window repaint, and the cost grows with the size of the app and the resolution of the
screen.

## Where it stands

A frame goes through five stages. The first two already scale with the change:

| Stage | Today | Proportional to the change? |
|---|---|---|
| Reactive update | Only the effects that read the changed state run | Yes |
| Layout | `place()` skips unchanged subtrees; a color change doesn't invalidate layout at all | Yes |
| Paint (UI thread) | `root->paint()` walks the whole tree and builds a new display list for the window | No |
| Plan (render thread) | `plan_frame` converts the whole list into instances | No |
| GPU and present | Clear the target, draw every instance, present the whole image | No |

Elegance already renders **only when something changed**: an idle window costs nothing. The goal
here is that a frame that does render costs only what changed.

## How it should work

### Invalidation: knowing what changed

Layout already has invalidation (`invalidate_layout`, `needs_arrange_`). Paint gets the same:

- `invalidate_paint()` marks an element paint-dirty and marks its ancestors "dirty inside".
- **Every** setter that changes how an element looks calls it: fills, colors, opacity, offsets,
  text, images, video frames, carets and selection, interaction states. A frame change from layout
  counts, and so do mounting, unmounting and transitions.
- Theme, color-scheme and font-scale changes affect everything and simply request a full redraw.

Today these setters only update their value and raise the window's "needs a frame" flag. Adding
the call is cheap if done while the setters are being written, and tedious if done later. That's
why it's phase 0 of the checklist.

### Damage: where it changed

After layout, the App collects **damage**: for each paint-dirty element, the union of its paint
bounds **before and after** the change. Old bounds cover where it was, new bounds where it is.
Paint bounds already exist (`update_paint_bounds`, `paint_extent`) and include shadows, strokes,
focus rings and an 8 pt margin for overhangs. Removed elements contribute their last bounds;
animating elements contribute their bounds every frame.

**Effects reach beyond the change.** A layer's shadow is a blur of everything inside it, so a
change to a child changes the shadow up to 3σ around the child (plus the shadow's offset).
Walking up from each changed element, damage grows by the blur extent and offset of every
ancestor that blurs or shadows its contents. The same applies later to `blur` and to backdrop
blurs (`background_material`), where a change *behind* the blurred view affects it within the
blur radius.

Damage is a short list of rects in window pixels. Overlapping rects merge, and too many rects
merge into fewer larger ones. If damage covers most of the window, or after a resize, scale
change or first frame, the whole window is damaged.

The damage travels with the `WindowFrame`. The mailbox **coalesces** frames: when a new frame
replaces a pending one, the new frame's damage is unioned with the dropped frame's, or the dropped
frame's changes would never reach the screen.

### Partial redraw on the GPU

Drawing only part of a frame requires the rest of the previous frame to still be there.
Swapchain images don't guarantee that, so each window keeps a **persistent canvas**:

- **Windows (DirectComposition)**: the shared texture already plays this role. Each frame is
  rendered into it and then copied into the composition swapchain. Its contents survive between
  frames.
- **Vulkan swapchain path**: an offscreen image of the window's size, blitted into the acquired
  swapchain image each frame. A full-window GPU copy costs very little compared with redrawing.

Each frame then:

1. Begins rendering with `LOAD_OP_LOAD` (keep the previous contents).
2. For each damage rect, sets the scissor to it, clears it to the background (a rect under the
   scissor, not a full clear), and draws **every instance that intersects it, in painter's
   order**. Not just the changed element: a translucent overlay above it or a background below it
   must be redrawn in the same order for blending to come out right. The scissor guarantees
   nothing outside the rect changes.

   This is what makes translucency work without any bookkeeping: an element never has to tell
   the elements above it that it changed. Whatever overlaps the damaged region (overlays,
   translucent panels, shadows) is redrawn there because it overlaps, and it blends over freshly
   drawn pixels. When an element's own opacity changes, its bounds are damaged, so whatever shows
   through it is redrawn underneath.
3. `plan_frame` gets the damage list and drops instances whose **drawn** bounds miss every rect,
   so the GPU only processes what's needed. Drawn bounds include the anti-aliasing margin and a
   box shadow's spread; testing only the instance's rect would skip an overlay whose edge or
   shadow reaches into the damage.

Offscreen layers that intersect damage are re-rendered whole at first; caching them comes later.

### Partial present

Telling the compositor what changed saves its work too:

- **DXGI**: `Present1` with the damage as dirty rects; DWM then recomposes only those regions.
- **Vulkan**: `VK_KHR_incremental_present` with the damage as present regions, where supported.

### Retained paint: cheap display lists

Partial drawing makes the GPU cheap, but the UI thread still walks every element to build the
list. To fix that, each element **caches the commands it painted**, in coordinates relative to its
own frame. Painting a clean subtree appends its cached commands, translated to its current
position. A parent moving (including scrolling) doesn't invalidate its children's caches, only
their position. Elements whose output depends on more than their own state (canvases, carets,
`always_arranges` elements) don't cache.

### Delta frames and cached layers

The next step moves retention to the render thread. It keeps each window's display list as
**chunks keyed by element id**, and the UI thread sends only changed chunks and removed ids.
Large windows then stop copying an unchanged list across threads every frame. Layer textures
(opacity groups, shadows, path clips) are cached by element id and content version, so a fading
panel doesn't re-render its contents every frame. A cached layer is only valid while **nothing
inside it** changed: any paint-dirty descendant invalidates the layer of every enclosing group,
which the "dirty inside" flag from invalidation provides.

### Scrolling

Scrolling moves everything in the viewport, so the viewport is damaged as a whole. That's still
much less than the window. A later optimization copies the still-visible part of the canvas by
the scroll delta on the GPU and draws only the newly exposed strip.

## Rules

- Every paint-affecting change invalidates the element it affects. No exceptions.
- Damage is old bounds ∪ new bounds, using paint bounds rather than frames, grown by the blur
  and shadow reach of enclosing effects.
- Region, not element: everything under the damage is redrawn, so overlapping and translucent
  views need no notification.
- Under a damage rect, redraw everything in painter's order.
- Merging pending frames merges their damage.
- When in doubt (environment changes, resizes), redraw everything.

## How to know it's right

Incremental rendering fails quietly: a missing invalidation leaves **stale pixels** that look
almost right. Three tools make failures visible:

1. **`ELG_DEBUG_DAMAGE=1`** flashes each frame's damage rects, which shows both missing damage
   (something changed with no flash) and excessive damage (a hover flashing the whole window).
2. **A validation mode** (debug builds) renders each frame both fully and incrementally offscreen,
   compares the two, and reports the first element whose change wasn't invalidated.
3. **Inspector stats**: damaged area as a percentage of the window, instances drawn vs total, and
   paint time.

## Edge cases

- **Pixel snapping**: damage rects must be rounded outward to whole pixels, or anti-aliased edges
  leave faint seams.
- **Transparent windows**: clearing inside the damage must clear to transparent, not to a color.
- **Videos** damage only their own rect each new frame, which also lets the render thread's
  video-only redraws become partial.
- **Multiple damage rects per pass** need either one scissored draw per rect or a stencil mask;
  start with per-rect draws and a low cap on the rect count.
