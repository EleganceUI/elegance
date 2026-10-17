---
title: Incremental rendering
description: Damage tracking, partial GPU redraws, partial presents, retained paint, and delta frames, so a small change costs a small amount.
sidebar:
  order: 4
---

A button changing color should cost about as much as the button, not the window. Today the
reactive update and layout are already that surgical, but painting rebuilds the whole display
list and the GPU clears and redraws the whole window. This page makes every stage proportional
to what changed.

:::note[Depends on]
[Display list and the UI ubershader](../ui-pipeline/) and [Views, modifiers, and the element tree](../../framework/views-and-elements/).
The full version also needs [Animation](../../framework/animation/), [Scrolling and lists](../../controls/scrolling-and-lists/)
and [Windows and DirectComposition](../../controls/windows/), so do phases 1–4 after the Controls
part. **Add the invalidation hooks (phase 0) as soon as elements exist.** Retrofitting them into
every setter later is the painful part.
:::

## Read first

- The spec for this page (link above), which explains the design and why each phase exists
- `Element::paint`, `update_paint_bounds`, `paint_extent` in `src/view/element.*`
- `build_frame` and `post_frame` in `src/app.cpp`; `plan_frame` in `src/render/frame_plan.cpp`
- `WindowTarget::render` in `src/render/window_target.cpp` (the clear load op), `src/gpu/composition_win32.*`
- [`VK_KHR_incremental_present`](https://registry.khronos.org/vulkan/specs/latest/man/html/VK_KHR_incremental_present.html), DXGI `Present1` with `DXGI_PRESENT_PARAMETERS` dirty rects

## Tasks

### Phase 0: invalidation hooks (do this early)
- [ ] `Element::invalidate_paint()`: marks this element paint-dirty and marks every ancestor "something inside is paint-dirty" (like `needs_arrange_`)
- [ ] Every paint-only setter calls it: fills, stroke colors, text color, opacity, offsets, z-index, shadow, image or video content, caret and selection, hover and pressed looks
- [ ] Layout changes count too: an element whose frame changes is paint-dirty
- [ ] Structural changes count: mounted and unmounted elements, and transitions in progress
- [ ] `ELG_ASSERT` in debug builds that nothing paint-affecting changes without an invalidation (see "Verification" below)

### Phase 1: damage tracking
- [ ] After layout, collect **damage**: for every paint-dirty element, the union of its **old** and **new** paint bounds (`paint_extent`, so shadows, strokes, focus rings and glyph overhangs are included)
- [ ] Removed elements damage their last bounds; added ones their new bounds
- [ ] Animating elements damage their old and new bounds every frame
- [ ] **Expand damage through effects**: walking up from a changed element, every ancestor that blurs or casts a shadow of its contents (a layer shadow, and later `blur` and `background_material`) grows the damage by its blur extent (3σ) and its shadow offset. A change inside a shadowed card changes the card's shadow beyond the child's own bounds
- [ ] Keep damage as a short list of rects in window pixels: merge overlapping ones, and cap the count (e.g. 8) by merging the closest pairs
- [ ] Above a threshold (e.g. 50% of the window), or after a resize, scale change or the first frame, damage the whole window
- [ ] Send the damage list with the `WindowFrame`; coalescing two pending frames **unions** their damage (a dropped frame's damage must not be lost)

### Phase 2: partial GPU redraw
- [ ] A **persistent canvas** per window that keeps the previous frame: on Windows the composition path's shared texture (already copied into the swapchain each frame), and on the plain Vulkan path an offscreen image blitted to the swapchain image
- [ ] Render with `LOAD_OP_LOAD` and set the **scissor** to each damage rect, so nothing outside changes
- [ ] `plan_frame(list, damage)`: drop instances whose **drawn** bounds (including the AA margin and a box shadow's 3σ spread) miss every damage rect; draw everything that intersects a rect, in painter's order (overlapping and translucent siblings must be redrawn too, not just the changed element)
- [ ] Layers that intersect damage are re-rendered whole (until phase 4 caches them)
- [ ] Clear only inside the damage (draw the clear color as a rect under the scissor), never the whole canvas
- [ ] Frames in flight: the canvas is shared by every frame slot, so a frame must not start drawing into it until the previous frame's copy out of it has finished. Check the existing fence and D3D event query cover this

### Phase 3: partial present
- [ ] Windows (DirectComposition): present with `Present1` and the damage as dirty rects, so DWM recomposes only those. Confirm with the DXGI docs whether flip-model back buffers must still be fully written (they're filled by the full copy from the canvas, so this is safe either way)
- [ ] Vulkan swapchains: `VK_KHR_incremental_present` with the damage as present regions, where the device supports it
- [ ] Without either, present normally; the canvas still saves the drawing work

### Phase 4: retained paint (CPU side)
- [ ] Each element caches the commands it painted last frame, in **local coordinates** relative to its frame
- [ ] `paint` re-paints a paint-dirty element; a clean subtree appends its cached commands, translated to its current position (so a parent moving or scrolling doesn't invalidate the children)
- [ ] Elements whose painted output depends on more than their own state (canvases, `always_arranges` elements) opt out of caching
- [ ] Measure: building the display list for a 1-element change in a 1,000-element window should cost a small fraction of a full paint

### Phase 5: delta frames and cached layers
- [ ] The render thread keeps the **retained display list** per window. The UI thread sends only changed chunks (keyed by element id) plus removed chunk ids
- [ ] Cache **layer textures** (opacity groups, shadows, path clips) keyed by element id and content version; reuse the texture while the subtree is clean
- [ ] Any paint-dirty descendant invalidates the cached layer of **every** enclosing group (through the "dirty inside" flag), not just a change to the group itself
- [ ] Glyph, image and path-mask caches already persist; make sure nothing in planning re-does per-frame work for unchanged chunks

### Phase 6: scrolling
- [ ] Scrolling damages the whole viewport. Optionally: copy the still-visible part of the canvas by the scroll delta on the GPU, and redraw only the newly exposed strip plus any fixed overlays (scroll bars)
- [ ] Measure before doing it; one damaged viewport is already far cheaper than the whole window

### Verification and tools
- [ ] `ELG_DEBUG_DAMAGE=1`: flash each frame's damage rects in a translucent color, so missing or oversized damage is visible
- [ ] A validation mode (debug only) that renders the frame both ways (full and incremental) offscreen and compares them, reporting the first element whose change wasn't invalidated
- [ ] Inspector frame stats: damaged area as a percentage of the window, instances drawn vs total, and paint time
- [ ] Tests: `plan_frame` with damage culls correctly; coalesced frames union damage; a removed element damages its old bounds

## Done when

- [ ] Hovering one button in a 1,000-element window redraws only that button's region (visible with `ELG_DEBUG_DAMAGE`)
- [ ] A translucent overlay above a changing view blends correctly over the new content, and a view whose opacity animates shows the correct pixels behind it every frame
- [ ] Changing a child inside a card with a shadow leaves no stale shadow edge
- [ ] A blinking caret damages only the caret's rect
- [ ] An indeterminate `ProgressView` animates by redrawing only its own bounds
- [ ] GPU time for a small change scales with the damaged area, not the window size (compare 1080p and 4K)
- [ ] The full-vs-incremental validation mode reports no differences across the sandbox's interactions
- [ ] Resizing, scale changes, theme switches, and the first frame still redraw everything correctly

## Gotchas to expect

- **Missing invalidations show up as stale pixels**, which are hard to notice and easy to ship.
  This is why the validation mode is a task, not a nice-to-have.
- **Draw everything under the damage, not just the changed element.** A translucent sibling on
  top, or a background behind, must be redrawn in order within the scissor, or blending comes out
  wrong.
- **Paint bounds must include everything drawn outside the frame**: shadows (3σ), focus rings,
  stroke widths, glyph overhangs, transition offsets. Damage built from frames alone leaves trails.
- **Coalesced frames**: if the mailbox replaces a pending frame, its damage must be merged into
  the new one, or the area it changed never gets redrawn.
- **Environment-wide changes** (theme, color scheme, font scale) dirty everything. Let them fall
  back to a full redraw rather than invalidating element by element.
