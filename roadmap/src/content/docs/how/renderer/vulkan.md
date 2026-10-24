---
title: Vulkan bring-up
description: "The GPU objects Elegance owns, their lifetimes, and how a window gets its images."
sidebar:
  order: 1
depends: [foundations/platform]
---

## What it's for

This layer gets pixels into a window. It owns the Vulkan instance, the logical device and
allocator, and each window's swapchain and per-frame synchronization. Nothing above the renderer
sees Vulkan; the rest of the framework only produces display lists.

## How it works

### Instance and validation

Vulkan is loaded at runtime with **volk**: no import library, and function pointers are fetched
once per instance and device. The instance enables the extensions SDL reports for surfaces (plus
the portability extension on macOS). Debug builds enable the Khronos validation layer by default,
overridable with `ELG_VULKAN_VALIDATION=0/1`. Its messenger callback is a catch-all trampoline:
in debug builds a validation **error** on a framework-owned object counts as an internal bug and
produces a crash report.

`Surface` is an RAII wrapper. The UI thread creates and destroys it (SDL's rule); the render
thread only borrows it, and finishes with it before the UI thread destroys it. The order is
enforced by a `RemoveWindowCommand` that the UI thread waits on.

### Device

The physical device is chosen by its ability to present to a **compatibility surface**, the first
window's. Requirements:

- **Vulkan 1.3**, with **dynamic rendering** (no render-pass or framebuffer objects; passes begin
  and end around attachments directly) and **synchronization2** (`vkCmdPipelineBarrier2`, clearer
  stage and access masks).
- One queue family that does graphics and present.

Optional extensions are recorded as they're enabled, so later features can check for them:
external memory on Win32 for D3D11 interop, and others. The device also publishes its **adapter
LUID**, because D3D11 devices for presentation and video must be created on the same GPU.

Memory goes through **VMA**. `Buffer` and `RenderImage` are RAII wrappers over an allocation plus
a view.

### Swapchain

A window's `Swapchain` uses an sRGB format and FIFO (vsync) present mode, with **two images**,
so at most one frame waits for display. That keeps latency low while resizing.

`prepare(fallback_extent, idle, submitted)` checks whether the swapchain is stale or the surface
size changed. If so, it:

1. calls `idle()`, which waits only for **this window's** frames in flight (never the whole device),
2. creates a new swapchain with the old one as `oldSwapchain`,
3. **retires** the old one: its image views and semaphores move to a retired list tagged with the
   current submission count.

`collect(completed)` frees retired entries once a later submission has completed. A present that
was queued before the swap may still wait on those semaphores, so they can't be destroyed at
recreation time.

Each swapchain image has its own `render_finished` semaphore. An image's previous present has to
finish before the image is acquired again, so a per-frame-slot semaphore could be signaled again
while still waited on.

`prepare` returns false for a zero-area surface (minimized), and the frame is skipped.
`VK_ERROR_OUT_OF_DATE_KHR` or a suboptimal result marks the swapchain stale for the next frame.

### Frames in flight

Each window has a small ring of **frame slots**. A slot holds a command buffer, a fence, an
acquire semaphore, per-frame descriptor pools, staging buffers to free later, and pooled
offscreen images. Before a slot is reused, its fence is waited. Every submission increments a
counter, and the newest submission known complete is tracked. Retired swapchains, staging
buffers and cached GPU objects use those numbers to decide when they're safe to free.

## Rules

- The render thread owns every Vulkan object except surfaces.
- Never `vkDeviceWaitIdle` for routine work (resizes, cache trims). Wait on the specific fences.
- A resource the GPU may still read is freed only after a submission that used it has completed.

## Edge cases

- **Transparent windows on Windows**: drivers expose only opaque composite alpha, so a
  transparent Vulkan swapchain isn't possible. Presentation goes through DirectComposition instead
  (see [Windows, title bars, and DirectComposition](../../controls/windows/)).
- **Device lost**: designed but not built. The render thread would recreate the device and every
  resource from UI-side handles, or report a `GpuError` if it can't.
- **Multiple GPUs**: the compatibility surface picks the GPU that drives the app's windows, and
  everything else follows its LUID.
