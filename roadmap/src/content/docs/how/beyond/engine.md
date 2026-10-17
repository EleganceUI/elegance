---
title: Renderer, text, and engine follow-ups
description: "Proposed designs for device loss, text shaping gaps, embedding, headless testing, and performance budgets."
sidebar:
  order: 3
depends: [renderer/ui-pipeline, text/glyphs, tools/quality]
---

:::caution[Design, not built]
These are directions, with the approach the design leans toward.
:::

## Renderer

- **Device lost**: the UI side keeps every resource as a **handle** (images by id, glyphs by atlas
  slot, paths by content). On `VK_ERROR_DEVICE_LOST`, the render thread drops every GPU object,
  recreates the device, and re-creates resources lazily from the next frames' references. The
  glyph atlas re-uploads from the disk cache. If recreation fails, a `GpuError` goes to
  `on_error`.
- **Damage and partial redraws** have their own spec: [Incremental rendering](../../renderer/incremental/).
- **Bindless images**: one descriptor array for all images, indexed per instance, so a screen of
  thumbnails is one draw instead of one per image.
- **Atlas growth**: atlas pages as array layers; LRU-evict glyphs not drawn for a while when all
  pages are full.
- **HDR and wide gamut**: an `RGBA16F` scRGB swapchain where available, colors as extended sRGB.
  This is mostly a decision about blending space first.

## Text

- **Bidi**: run the Unicode Bidirectional Algorithm (SheenBidi or ICU4X) per paragraph, shape each
  directional run, reorder runs per line after breaking, and map carets visually.
- **Script itemization**: split runs by script before shaping, so HarfBuzz gets correct script and
  language tags.
- **UAX #14 line breaking** and **UAX #29 graphemes**: replace the "after whitespace, between CJK"
  rule and cluster-based caret stops.
- **Bitmap emoji** (CBDT/sbix): an RGBA atlas per size bucket, sampled with the `image` kind.
- **Large documents**: a rope with per-line layout caches and a lazy-stack-style virtualized view.

## Embedding mode

No platform layer and no render thread: the host owns the window and device. The host passes
input events and calls `elg::render(command_buffer, target)` with the latest frame packet on its
own thread. Popups fall back to an in-window top layer.

## Headless testing

An offscreen `WindowTarget` that renders into an image and reads it back, plus a synthetic input
API on the dispatcher. That makes golden-image tests possible: render a view, compare with a
stored PNG within a tolerance. lavapipe (CPU Vulkan) runs them in CI.

## Profiling and crashes

- **Tracy** zones around the frame stages (events, effects, layout, paint, plan, record, present)
  behind the `profiling` feature.
- **Crashpad** for out-of-process minidumps of hard faults behind `crash-reports`; the in-process
  reporter stays for assertions.

## Performance budgets

The design's targets, and how to measure each:

| Budget | Target | Measure |
|---|---|---|
| Cold start to first frame | < 100 ms | Timestamp at `main` → first present |
| Idle | ~0% CPU/GPU | Process monitor over 10 s idle |
| Hover change, 1k elements | < 0.5 ms CPU | Tracy zone around the frame |
| Full relayout, 1k elements | < 2 ms CPU | Resize-triggered layout zone |
| Draw calls per typical frame | < 20 | Count in `plan_frame` |
| 100k-row list scroll | 60+ fps steady | Frame times during a scripted scroll |
| Hello world memory | < 30 MB | Working set after first frame |
