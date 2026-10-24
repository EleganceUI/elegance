---
title: MSDF glyphs, the atlas, and the disk cache
description: msdfgen MTSDF generation on worker threads, the GPU atlas, and a persistent glyph cache.
sidebar:
  order: 2
---

Glyphs are multi-channel signed distance fields (MTSDF): one atlas entry serves every size,
scale and DPI. They're generated off the UI thread and cached on disk between runs.

:::note[Depends on]
[Fonts, shaping, and the Text view](../fonts-and-layout/) and the
[render thread](../../renderer/render-thread/) (`AtlasUploadCommand`).
:::

## Read first

- `design.md` §5 (Text rendering: MSDF), "Rendering foundations" (worker pool, disk cache)
- `src/text/glyph_atlas.*`, `src/text/atlas.h`, `src/text/glyph_cache.*`, `src/text/glyph_draw.h`, `src/gpu/atlas_texture.*`

## Tasks

### Generation
- [ ] FreeType outline → msdfgen shape → **MTSDF** (RGB multi-channel + true SDF in alpha), with msdfgen's error correction
- [ ] Settings: 48 px em, 6 px distance range, 2048² RGBA8 atlas
- [ ] Shader: `median(r, g, b) - 0.5`, scaled by the on-screen distance range for a 1 px AA ramp

### Atlas (UI side)
- [ ] On first use, the UI thread reads the outline, computes metrics and reserves an atlas slot (shelf packing), so layout and drawing never wait
- [ ] Distance fields generate on a worker pool. Workers: hardware threads − 1, at most 8; **3 in debug builds**
- [ ] Finished glyphs queue as `GlyphUpload`s; `take_uploads()` posts them before any frame that uses them; a callback wakes the event loop to redraw
- [ ] `wait(timeout)`: new windows wait up to 1.5 s for their glyphs before showing (popups 150 ms)

### Atlas (GPU side)
- [ ] `AtlasTexture`: an RGBA8 texture plus its descriptor set; copy uploads in on the render thread

### Disk cache
- [ ] One file per font, named by a hash of the font's bytes, in `%LOCALAPPDATA%\Elegance\GlyphCache` (macOS `~/Library/Caches/Elegance/GlyphCache`, Linux `$XDG_CACHE_HOME/elegance/glyphs`)
- [ ] `ELG_GLYPH_CACHE` overrides the path; `ELG_GLYPH_CACHE=off` disables it
- [ ] Header with format version and generator settings; records appended by workers, each with a checksum
- [ ] On load, stop at the first bad record and truncate there; cap each file at 64 MB
- [ ] A cache hit uploads at once, with no worker job

### Icon fonts
- [ ] Fonts flagged by `register_icon_set` get a **single-channel** true distance field with non-zero scanline sign correction, copied into all channels
- [ ] Reverse outlines in the PostScript direction (`FT_Outline_Get_Orientation`) before generating

## Done when

- [ ] Text scales smoothly from 8 px to 200 px with no re-generation
- [ ] Second launch of the sandbox is much faster than the first (the design measured 2.17 s cold vs 0.68 s warm in debug)
- [ ] Killing the app mid-write leaves a cache that loads up to the torn record

## Gotchas

- **The debug heap has a single lock.** Eight workers ran 2.5× slower than three in debug builds.
- **Multi-channel fields break icon fonts**: overlapping outlines turned into speckles and streaks.
  Icons need a single-channel field. Text keeps MTSDF.
- **Phosphor's outlines rendered inside-out** until outlines in the PostScript direction were
  reversed.
- Verify glyph generation against FreeType's own rasterization of the same glyphs.
- Not built yet: atlas eviction (LRU) and extra pages when the atlas fills, and a hinted bitmap
  path for very small sizes if measurement shows it's needed.
