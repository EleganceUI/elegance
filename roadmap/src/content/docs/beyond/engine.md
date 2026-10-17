---
title: Renderer, text, and engine follow-ups
description: Rendering, text, performance, testing and embedding work the design still lists.
sidebar:
  order: 3
---

## Tasks

### Renderer
- [ ] Device-lost recovery: recreate the device and resources from UI-side handles, else marshal a `GpuError`
- [ ] HDR and wide gamut; a decision on sRGB-correct vs web-compatible blending
- [ ] Optional `MAILBOX` present mode for low latency (damage and partial redraws are their own page: [Incremental rendering](../../renderer/incremental/))
- [ ] Bindless descriptor indexing for images (one draw for many images)
- [ ] Glyph atlas eviction (LRU) and pages
- [ ] A keep-ready popup window, so menus open without creating a D3D device and swapchain
- [ ] A Vello-style compute rasterizer, if path-heavy scenes need it

### Text
- [ ] Bidi (SheenBidi or ICU4X), script itemization, UAX #14 line breaking, UAX #29 graphemes
- [ ] Variable font axes
- [ ] CBDT/sbix bitmap emoji; COLRv1 sweeps, two-circle radials, composite modes, clip boxes
- [ ] A hinted bitmap path for small sizes, if measurements call for it
- [ ] A large-document strategy (rope + virtualized lines), probably post-v1

### Embedding and tooling
- [ ] Embedding mode: render into a host Vulkan app (`elg::render(cmd)`), with in-window popups
- [ ] Headless rendering for golden-image tests; synthetic input API
- [ ] Tracy profiling behind the `profiling` feature
- [ ] Crashpad (or sentry-native) for out-of-process hard-fault reports
- [ ] Theme token files (JSON/TOML) with hot reload

### Performance budgets to verify (design §17)
- [ ] Cold start to first frame under 100 ms
- [ ] Hover change in a 1,000-element UI under 0.5 ms CPU
- [ ] Full relayout of 1,000 elements under 2 ms CPU
- [ ] Under 20 draw calls in a typical frame
- [ ] A 100k-row virtual list scrolling at a steady 60+ fps
- [ ] Hello world under 30 MB of memory
