---
title: Render thread and frame mailbox
description: A separate render thread fed by coalesced frame packets, with live resize.
sidebar:
  order: 2
---

The UI thread never touches Vulkan, and the render thread never touches the element tree or
signals. Frames cross between them as immutable packets.

:::note[Depends on]
[Vulkan bring-up](../vulkan/).
:::

## Read first

- `design.md` §15 (Threading Model)
- `src/render/frame_mailbox.*`, `src/render/render_thread.*`, `src/render/window_target.*`

## Tasks

### Mailbox
- [ ] `WindowFrame { window_id, pixel_width, pixel_height, scale, DisplayList }`, owned by value
- [ ] Commands: `AddWindowCommand` (surface, transparent, native handle), `RemoveWindowCommand`, `AtlasUploadCommand` (new glyph pixels)
- [ ] `post(command)` / `post(frame)` return a sequence number
- [ ] **Coalescing**: a new frame for a window replaces that window's pending frame. Commands queue in order and run before frames
- [ ] `wait_until_done(sequence, timeout)` on the UI thread; `take()` / `take_for(timeout)` / `mark_done()` on the render thread
- [ ] `request_stop()` and `close()`, which releases every waiter when the render thread exits

### Render thread
- [ ] Constructor starts the thread and blocks until the GPU is initialized, rethrowing init errors
- [ ] Owns the device and every GPU resource; one `WindowTarget` per window
- [ ] Errors: store the `exception_ptr`, call `on_error` so the UI loop stops, rethrow from `App::run()`
- [ ] Teardown order: stop the render thread first, then destroy surfaces, then windows, then the instance, then SDL

### Showing windows and resizing
- [ ] First frame: lay out, wait briefly for glyphs, post, `wait_until_done` with a timeout, *then* show the window, so it never appears blank
- [ ] Live resize: from the platform's live-resize callback, build and post a frame, wait for it to be presented, then call `wait_for_compositor()` so the new frame is on screen before the next resize step
- [ ] Bound every UI-thread wait with a timeout so a stuck driver can't freeze the app

### Idle and pacing
- [ ] Render only when something is dirty; otherwise block in `wait_event`
- [ ] While animating: render every frame, paced by waiting for the previous frame's present
- [ ] Measure idle CPU: it must be 0%

## Done when

- [ ] Resizing by dragging an edge shows no freezing and no stretched frames
- [ ] Posting 100 frames for one window while the render thread is blocked leaves exactly one pending
- [ ] An exception thrown on the render thread surfaces as an exception from `App::run()`
- [ ] Process Explorer shows 0% CPU for an idle window

## Gotchas

- **Clear `dirty` before building a frame, not after.** The old loop cleared it afterwards and
  dropped redraws requested during layout.
- Apply event results as `if (call()) dirty = true;` so a handler's own redraw request can't be
  overwritten by a later `dirty = call()`.
- Design follow-ups still open: `VK_EXT_swapchain_maintenance1` 1:1 present scaling, and shared
  fences instead of CPU waits around the D3D11 copy.
- Embedding mode (a host app calling `elg::render(cmd)` on its own render thread) is designed
  but not built.
