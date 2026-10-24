---
title: Frame cost and pacing
description: "Where a frame's time goes today, the bugs that made it expensive, and the next steps to make an animating app cost little and a hidden one nothing."
sidebar:
  order: 5
depends: [renderer/render-thread, renderer/incremental, framework/animation, controls/windows]
---

:::caution[Partly built]
Frame pacing and culling are fixed (see "Fixed so far"). The rest of this page is measured
findings and the design for the next steps.
:::

## What it's for

An idle Elegance window already costs nothing: no state changes, no frames. This page is about
the other case: something is moving. That might be a sidebar sliding, a spinner, the music
app's "now playing" bars, or a scroll. Moving things should cost about as much as the things
that move. Windows nobody can see should cost nothing.

## Where it stands

Measured on an RX 6650 XT at 1920×1080, 75 Hz, release build. The test app was `music-cpp` (elegance-examples) at
1280×800, with an animation kept running (no input).

| Stage, per frame | Listen Now (~800 commands) | Songs page, 123 rows (~1,200 commands) |
|---|---|---|
| Layout (UI thread) | 0.2–0.25 ms | 0.9 ms (every row, every frame) |
| Paint: build the display list | 0.2 ms | 0.23 ms |
| Plan + record (render thread) | 0.25 ms | 0.2 ms |
| GPU (submit to fence) | 0.45 ms | 0.3 ms |
| Present: D3D11 copy + spin-wait | 0.3 ms | 0.3 ms |
| **Whole app** | **75 fps, ~9% of a core, ~4% GPU** | **75 fps, ~14% of a core, ~3% GPU** |

Each frame is cheap. The cost comes from running full frames continuously: a 13×12 pt animation
pays for a full layout, a full display list, and a full-window redraw and copy, 75 times a
second, for as long as it runs.

### Fixed so far

- **Pacing.** Composition swapchains presented with sync interval 0. That made the frame-latency
  waitable signal as soon as a frame was queued, so it never paced anything. Any animation
  rendered at ~900 fps (0.42 ms of GPU each, ~50% of the GPU). Now: interval 1, 75 fps, ~4%.
  Details in [Windows and DirectComposition](../../controls/windows/).
- **Culling.** `ScrollView` culls children whose paint bounds miss the visible area. But `If`,
  `ForEach` and `View()` are flattened into their parent layout and never placed, so they had
  no paint bounds. `update_paint_bounds` read that as "draws anywhere". Any row containing a
  conditional was never culled, so a 123-row list painted all 123 rows. Paint bounds now look
  through flattened children (`Element::flattened_by_layouts`). The Songs page went from 3,918
  to 1,165 commands per frame, and paint from 0.74 to 0.23 ms.

## How it should work

In order of value for effort. Each one is independent.

### 1. Measure inside the framework

Every number above came from throwaway timers and a scenario hook, removed afterwards. Make
them permanent:

- `ELG_PROFILE=1` logs, every couple of seconds per window: frames per second, average and worst
  time for each stage (layout, paint, plan, record, GPU, present), and draw commands per frame.
- GPU timestamp queries around each pass (quads, layers, blurs, path masks, video conversion).
  The GPU breakdown is unknown today. 0.45 ms for ~600 instanced quads at 1280×800 seems high
  for this GPU and is worth a look once it can be seen per pass.
- The same numbers in the inspector's frame stats, which today show only build time and counts.
- Scenario hooks that animate without input (for example a flag that toggles a sidebar every
  450 ms), so a change can be measured before and after without touching the desktop.

### 2. Invisible windows render nothing

A minimized window with a running animation still builds and renders 75 frames a second
(measured: ~5% of a core, ~3% GPU, for nothing on screen). The render thread skips minimized
windows only when the swapchain is zero-sized, and on Windows it isn't.

- A window that is minimized, cloaked (`DWMWA_CLOAKED`: another virtual desktop, or hidden by
  the shell), or fully occluded stops building frames.
- Animations keep their clocks. On the way back, the first frame samples the current time, so
  animations jump to where they'd be, as if they had kept running.
- State changes while hidden just mark the window dirty, and one frame is drawn when it shows.

### 3. Things out of sight don't animate

`Canvas` is never culled (`cullable()` is false, because it may draw outside its frame), and a
canvas that calls `request_frame()` keeps its window at full frame rate even when it's scrolled
out of view. The music app's now-playing bars do exactly this whenever the playing row is
offscreen.

- A canvas records the bounds of what it drew last frame and is culled by those. The first
  frame, or one with no drawing, uses its frame.
- A culled element's frame requests are dropped. It gets a frame again when it scrolls back into
  view, because scrolling itself draws.
- The same applies to anything self-animating (`ProgressView`, video): out of sight means no
  frames.
- Optional: a minimum interval for decorative animations, so the bars could run at 30 fps.

### 4. Lay out only what moves

`build_frame` runs a full layout whenever any animation is running
(`FullLayoutScope(live.animating)`), because animated frames move elements without invalidating
layout. On the Songs page that's 0.9 ms of layout per frame to animate three bars, and the bars
don't change layout at all.

- An element whose frame is animating already marks itself `moving_`, and its ancestors see it
  through `update_paint_bounds`. Use the same mechanism for layout: arrange only along paths
  that contain moving elements, not the whole tree.
- Frames requested by drawing alone (`Canvas` time, `request_frame`) need no layout. Only
  repaint.
- Expected result: layout cost while animating scales with what animates, like it already does
  for state changes.

### 5. The present doesn't block the CPU

Each composition frame, the render thread waits on the CPU twice:

1. `vkWaitForFences` until Vulkan finishes rendering into the shared texture (~0.3–0.5 ms),
   because D3D11 copies it next.
2. A spin on an event query (`SwitchToThread`) until the D3D11 copy finishes (~0.3 ms), because
   Vulkan renders into the same texture next frame.

Both stalls serialize the CPU and GPU, and the spin burns a core while it waits. A shared fence
removes both:

- Create an `ID3D11Fence` with a shared handle (`D3D11_FENCE_FLAG_SHARED`). Import it into
  Vulkan as a timeline semaphore (`VK_KHR_external_semaphore_win32`, handle type
  `D3D12_FENCE_BIT`).
- Vulkan signals value N when rendering ends. D3D11 `Wait(fence, N)` on the GPU, copies, and
  signals N+1. The next Vulkan submit waits for N+1 before writing to the texture.
- The render thread then only waits for the frame-latency waitable, which is the pacing it
  actually wants.

### 6. Copy only what changed

The composition path copies the whole shared texture into the swapchain every frame: 4 MB at
1280×800, 33 MB at 4K. Once [incremental rendering](../incremental/) produces damage rects,
copy only those (`CopySubresourceRegion` per rect) and present them as dirty rects
(`Present1`). That plan already covers the redraw itself. The copy is the piece specific to this
path.

### 7. Incremental rendering

[Incremental rendering](../incremental/) is the large one: paint, plan and GPU work proportional
to the damage. Items 3 and 4 above don't depend on it and remove most of the cost of small
animations sooner.

## Rules

- **Measure before and after.** Every change here comes with numbers from the same scenario on
  the same machine. Several plausible-looking costs turned out not to matter, and the two real
  bugs didn't look like anything until measured.
- **Count frames, not just frame time.** Both bugs above had cheap frames. The cost was how many
  frames ran, or how much of the tree each one touched.
- **A cost nobody can see must be zero:** hidden windows, offscreen animations, culled subtrees.

## How to know it's right

- With `ELG_PROFILE=1`, an animating window shows the display's refresh rate, never more.
- A minimized or cloaked window with a running animation shows no frames, and the app's GPU
  and CPU counters (Task Manager, or `\GPU Engine(pid_<PID>_*engtype_3D)\Utilization Percentage`)
  read zero.
- On the Songs page with the playing row scrolled away, the window stops drawing (no frames at
  all).
- With a sidebar sliding on the Songs page, layout time per frame is a small fraction of a full
  layout.
- The render thread's CPU time per composition frame drops by the two stalls (~0.6–0.8 ms
  today).

## Edge cases

- **Coming back from minimized** must draw before it's shown, or the old frame flashes. The
  first frame after restoring is a full frame.
- **A canvas that draws far outside its frame** must still be culled by what it drew, not by
  its frame.
- **Shared fences** need the D3D11.3 device interface (`ID3D11Device5`). Keep the event-query
  path as a fallback where it's missing.
- **Animations that end while hidden** must still run their completion and settle their final
  values on the next frame.
