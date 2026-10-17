---
title: Vulkan bring-up
description: Instance, device, swapchain, and a cleared window.
sidebar:
  order: 1
---

Get a window cleared to a color through Vulkan 1.3, with validation layers clean. Nothing is
drawn yet.

:::note[Depends on]
[Platform layer](../../foundations/platform/).
:::

## Read first

- `design.md` §5 (Baseline, Frame pacing & power), §15
- `src/gpu/vk.*`, `src/gpu/vma.cpp`, `src/gpu/instance.*`, `src/gpu/device.*`, `src/gpu/swapchain.*`, `src/gpu/buffer.*`, `src/gpu/render_image.*`

## Tasks

### Loading and instance
- [ ] Load Vulkan with **volk** (no static link to the loader); compile VMA once in its own `.cpp`
- [ ] `Instance`: the extensions SDL asks for, plus the portability extension on macOS
- [ ] Validation layer and debug messenger: on by default in debug builds, `ELG_VULKAN_VALIDATION=0/1` overrides
- [ ] Debug messenger callback: a catch-all trampoline. In debug builds, a validation error on a framework-owned object is a crash report
- [ ] `Surface` RAII wrapper: created and destroyed on the UI thread, borrowed by the render thread

### Device
- [ ] Pick a physical device that can present to a compatibility surface (the first window's)
- [ ] Require Vulkan 1.3 with dynamic rendering and synchronization2 (no render pass objects)
- [ ] One graphics+present queue family
- [ ] Record which optional extensions were enabled (`VK_KHR_external_memory_win32` and others come later)
- [ ] VMA allocator; `Buffer` and `RenderImage` RAII wrappers on top of it
- [ ] Publish the adapter LUID: later, D3D11 devices for composition and video must be on the same GPU

### Swapchain
- [ ] sRGB color format, FIFO present mode (vsync)
- [ ] Two images, so at most one frame is queued (lower latency while resizing)
- [ ] `prepare()` recreates when stale or resized; returns false for a zero-area (minimized) surface
- [ ] One `render_finished` semaphore **per image**, since an image's previous present must finish before it's acquired again
- [ ] Retire old swapchains: keep their views and semaphores until a later submission has completed, because a pending present may still wait on them
- [ ] Handle `VK_ERROR_OUT_OF_DATE_KHR` / suboptimal by forcing recreation
- [ ] Transparent windows: prefer premultiplied composite alpha where the surface offers it

### Frames in flight
- [ ] Per-window frame slots, each with a command buffer, fence and acquire semaphore
- [ ] Count submissions; track the latest one known complete (used to free retired resources)

## Done when

- [ ] A window clears to a solid color with zero validation messages
- [ ] Dragging the window edge for 10 seconds causes no validation errors and no stretching
- [ ] Minimizing and restoring works; closing exits cleanly with no leaked-object warnings

## Gotchas

- **Windows Vulkan drivers expose only `VK_COMPOSITE_ALPHA_OPAQUE`**. Asking SDL for a transparent
  window alone gives black corners. Real transparency goes through DirectComposition (see
  [Windows and title bars](../../controls/windows/)).
- Recreating a swapchain should wait only on that window's own frames, never `vkDeviceWaitIdle`
  on the whole device. Waiting on everything makes resizing visibly laggy.
- Swapchain todo still open in the design: device-lost recovery, HDR/wide gamut, lavapipe for CI
  screenshots.
