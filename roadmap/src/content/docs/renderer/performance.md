---
title: Frame cost and pacing
description: Profiling built in, invisible windows and offscreen animations costing nothing, layout only for what moves, and a present that doesn't stall the CPU.
sidebar:
  order: 5
---

Small animations currently pay for full frames: a full layout, a full display list, and a
full-window redraw and copy. This page lists the steps that don't need
[incremental rendering](../incremental/), in order of value for effort. The spec has the
measurements behind each one.

:::note[Depends on]
[Render thread and frame mailbox](../render-thread/), [Animation](../../framework/animation/),
[Windows and DirectComposition](../../controls/windows/). Step 6 needs
[Incremental rendering](../incremental/).
:::

## Read first

- The spec for this page (link above), especially "Where it stands"
- `build_frame` and `post_frame` in `src/app.cpp` (`FullLayoutScope`)
- `Element::paint`, `update_paint_bounds`, `cullable`, `drawing_moves` in `src/view/element.*`
- `WindowTarget::render_composition` in `src/render/window_target.cpp` and
  `CompositionTarget::present` in `src/gpu/composition_win32.cpp`
- `CanvasElement` in `src/view/element.h`

## Tasks

### Already done

These are fixed in the code (not checkboxes, so they don't count toward this page's progress):

- Composition swapchains present with sync interval 1, so the frame-latency waitable paces frames (was ~900 fps and ~50% GPU while animating; now 75 fps and ~4%)
- Paint bounds look through `If`/`ForEach`/`View()` (`flattened_by_layouts`), so subtrees with conditionals are culled when scrolled away (Songs page: 3,918 → 1,165 commands, paint 0.74 → 0.23 ms)

### Step 1: profiling built in
- [ ] `ELG_PROFILE=1`: per window, every ~2 s, log fps plus average and worst ms for layout, paint, plan, record, GPU and present, and commands per frame
- [ ] GPU timestamp queries per pass (quads, layers, blurs, path masks, video conversion), shown in the same log
- [ ] Find out why ~600 instanced quads take ~0.45 ms of GPU at 1280×800 on an RX 6650 XT
- [ ] Inspector frame stats show the same breakdown
- [ ] A way to run a scenario without input (an app flag or hook that keeps an animation running), for before/after numbers

### Step 2: invisible windows render nothing
- [ ] Minimized, cloaked (`DWMWA_CLOAKED`) or fully occluded windows build no frames (today a minimized animating window still renders 75 fps)
- [ ] Animations keep their clocks while hidden; the first frame back samples the current time
- [ ] Changes while hidden mark the window dirty; one full frame is drawn before it shows again

### Step 3: out-of-sight animations stop
- [ ] `CanvasElement` records the bounds of what it drew and becomes cullable by them (its frame until it has drawn)
- [ ] Frame requests (`request_frame`, self-animating elements) from culled elements are dropped
- [ ] `ProgressView`, video and other self-animating elements get the same treatment
- [ ] Optional: a minimum frame interval for decorative animations

### Step 4: layout only what moves
- [ ] Replace `FullLayoutScope(live.animating)` with arranging only the paths that contain moving elements (`moving_` already propagates up)
- [ ] Frames requested only for drawing (canvas time, `request_frame`) skip layout
- [ ] Measure: sidebar sliding on the Songs page, layout per frame vs today's 0.9 ms

### Step 5: present without CPU stalls
- [ ] Shared `ID3D11Fence` imported into Vulkan as a timeline semaphore (`VK_KHR_external_semaphore_win32`)
- [ ] Vulkan signals N; D3D11 waits N on the GPU, copies, signals N+1; the next Vulkan submit waits N+1
- [ ] Remove the `vkWaitForFences` before the copy and the `SwitchToThread` spin after it
- [ ] Fall back to today's waits when `ID3D11Device5` isn't available

### Step 6: copy only the damage (after incremental rendering)
- [ ] `CopySubresourceRegion` per damage rect instead of copying the whole texture
- [ ] `Present1` with the damage as dirty rects

## Done when

- [ ] An animating window never exceeds the display's refresh rate (`ELG_PROFILE`)
- [ ] A minimized window with an animation running shows 0% GPU and ~0% CPU for the app
- [ ] The music app playing a song, with the playing row scrolled out of view and nothing else moving, draws no frames
- [ ] Layout time while a sidebar slides is a small fraction of a full layout
- [ ] The render thread no longer blocks on the GPU or spins in the composition present

## Gotchas to expect

- **Cheap frames can still be the problem.** Check frames per second and how much of the tree
  each frame touches, not just milliseconds per frame.
- **Don't trust a harness that didn't do anything.** A PowerShell `Add-Type` with `\"` escapes
  failed silently, so "minimized" and "wide window" runs measured an ordinary window. Check
  that the scenario actually happened (`IsIconic`, the logged frame size).
- **Culling bugs don't look like bugs**: everything still draws correctly, just all of it. Count
  draw commands against what's on screen.
- **The real pointer moves.** Hover effects with zero delay (split-view dividers) light up in
  screenshots when the user's mouse passes over the window. Retake the screenshot before
  calling it a regression.
