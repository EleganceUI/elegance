---
title: Render thread and frame mailbox
description: "How frames cross from the UI thread to the GPU without either side waiting on the other."
sidebar:
  order: 2
depends: [renderer/vulkan]
---

## What it's for

GPU work (recording, submitting, waiting for vsync) must never block input, reactivity or layout.
A dedicated render thread owns the device. The UI thread hands it **frames**, immutable packets
describing what to draw, through a mailbox that never builds a backlog.

## How it works

### The split

| UI thread | Render thread |
|---|---|
| Events, reactive state, environment, layout, animation ticks | Device, queues, swapchains, every GPU resource |
| Builds a `DisplayList` per window | Plans it into GPU work, records, submits, presents |
| Allocates resource *handles* (images, glyph slots) | Creates the backing GPU objects |
| Never touches Vulkan, except creating and destroying surfaces | Never touches elements or signals |

### Packets

A `WindowFrame` holds a window id, the pixel size, the scale, and a `DisplayList`, all by value.
Display lists hold **copies**: strings, polygons and decoded images are shared through
`shared_ptr<const ...>`, never as pointers into app memory. The packet stays valid however long
the render thread takes, and hot-reloaded modules can unload safely.

Besides frames there are **commands**: add a window (surface, transparency, native handle),
remove a window, and upload new glyph pixels to the atlas.

### The mailbox

`FrameMailbox` is a mutex, two condition variables, a command queue, and one pending frame **per
window**.

- `post(frame)` **replaces** that window's pending frame if one is waiting. If the GPU falls
  behind, the stale frame is dropped, not queued.
- `post(command)` appends; commands run in order, before frames.
- Every post returns a sequence number. `wait_until_done(sequence, timeout)` blocks the UI thread
  until the render thread has finished everything up to it.
- The render thread calls `take(batch)` (block until work arrives) or `take_for(batch, timeout)`
  (wake periodically, for video). It runs the batch, then `mark_done(sequence)`.
- `close()` on exit drops pending work and releases every waiter, so the UI thread can't hang on a
  dead render thread.

### When the UI thread waits

Waiting is the exception, and every wait has a timeout so a stuck driver can't freeze the app:

- **Showing a window**: lay out, give glyph workers a moment (1.5 s for windows, 150 ms for
  popups), post, and wait for the frame before `show()`. A window never appears blank.
- **Live resize**: each resize step posts a frame, waits for it, then waits for the compositor,
  so the OS never shows a stretched old frame.
- **Animating**: the loop waits for the previous frame's present before building the next, so
  it's paced by vsync rather than spinning.
- **Removing a window**: wait for `RemoveWindowCommand` before destroying its surface.

### The render thread's loop

1. Take a batch.
2. Run commands: create or destroy `WindowTarget`s, upload glyphs into the atlas texture.
3. For each frame: `plan_frame` (see [Display list and the UI ubershader](../ui-pipeline/)), then
   record and submit it into that window's target.
4. Mark the batch done.

Windows showing video are a special case. The render thread keeps the last frame of each such
window and, while a video plays, wakes every 4 ms to check for a newly decoded frame. When there
is one, it redraws the kept frame with no UI-thread work at all.

### Startup, errors, and shutdown

The constructor starts the thread and blocks until the device exists, rethrowing any
initialization error (no GPU, say) on the UI thread. Later, an `elg::Error` on the render thread
stops it, is stored, and wakes the UI loop through `on_error`; `App::run()` rethrows it.

Teardown order is fixed: stop the render thread (which destroys window targets, then pipelines
and the atlas, then the device), then destroy surfaces, then windows, the instance, and SDL.

### Redrawing only when needed

The UI loop keeps a `dirty` flag per window. Reactive effects that change painted properties,
layout invalidations, hover changes and timers set it. With nothing dirty and nothing animating,
the loop blocks in `wait_event` and the process uses no CPU.

A frame that does render currently repaints and redraws the **whole** window. Making that cost
proportional to what changed is the job of [Incremental rendering](../incremental/).

## Rules

- Packets are immutable once posted, and own everything they reference.
- Frames coalesce per window; commands never coalesce.
- Clear `dirty` **before** building a frame, so requests made during layout survive.
- Every UI-side wait has a timeout.

## Edge cases

- **Frames for a removed window** that are still pending are dropped.
- **Several windows**: each has its own pending frame, target and swapchain. A slow window doesn't
  hold back the others' packets, though they share one thread.
- **Embedding mode** (not built): no render thread; the host calls `elg::render(cmd)` with the
  latest packet on its own thread.
