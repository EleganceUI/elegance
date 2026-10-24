---
title: MSDF glyphs, the atlas, and the disk cache
description: "Why glyphs are distance fields, how they're generated off-thread without making layout wait, and how they persist."
sidebar:
  order: 2
depends: [text/fonts-and-layout]
---

## What it's for

Glyphs must render crisply at any size, scale and DPI, animate scale without re-rasterizing, and
appear quickly on first launch. Multi-channel signed distance fields (MSDF) give size
independence. A worker pool and a persistent cache make generating them cheap.

## How it works

### Why MTSDF

A distance field stores, per texel, the distance to the glyph's edge. The shader thresholds it
at 0.5 with a one-pixel ramp scaled to the on-screen size, so one bitmap serves every size. A
single-channel field rounds sharp corners. **Multi-channel** fields (MSDF) keep corners sharp by
storing three distances whose **median** recovers the edge. Elegance stores **MTSDF**: MSDF in
RGB plus a true SDF in alpha, which leaves room for outlines, glow and faux-bold later.

Generation settings: glyphs at a **48 px em**, a **6 px** distance range, in a **2048² RGBA8**
atlas. The shader converts the range to screen pixels from the em size it's drawn at.

### From outline to atlas slot (UI thread)

When a frame first needs a glyph, `GlyphAtlas::glyph(face, id)`:

1. reads the outline from FreeType, and **reverses it** if it's in PostScript direction
   (`FT_Outline_Get_Orientation`), because msdfgen expects TrueType winding;
2. computes the bitmap size (the glyph at 48 px plus the range margin) and its quad metrics in em
   units relative to the pen position;
3. **reserves a slot** by shelf packing with a 1 px gap;
4. checks the **disk cache**: a hit queues the pixels for upload right away;
5. otherwise queues a `GlyphJob` with everything a worker needs, independent of FreeType.

The UI thread never waits. Layout and painting use the metrics immediately, and until the
pixels arrive the slot is empty, so the glyph draws as nothing for a frame or two.

### Workers

A pool of threads (hardware threads − 1, at most 8; **3 in debug builds**) takes jobs, builds an
msdfgen shape from the outline, runs edge coloring, generates the MTSDF with error correction,
flips rows (msdfgen is bottom-up), and appends the result to the finished list and the disk cache.
A callback wakes the event loop. Jobs not yet started are dropped if no longer wanted.

**Icon fonts** take a different path. Their outlines overlap on purpose (strokes, rings), which
multi-channel generation turns into speckles. They get a **single-channel true SDF** with
non-zero scanline sign correction, copied into all four channels, so the shader's median still
works.

### Uploads

`take_uploads()` returns finished pixels, which the App posts as an `AtlasUploadCommand`
**before** the frame that draws them. The render thread copies them into the atlas texture.
`has_new()` tells the App which windows need a redraw because glyphs arrived.

New windows call `wait(timeout)` before showing (1.5 s, popups 150 ms), so the first frame
usually has its text.

### The disk cache

`GlyphDiskCache` stores generated pixels between runs:

- **Location**: `%LOCALAPPDATA%\Elegance\GlyphCache` (macOS `~/Library/Caches/Elegance/GlyphCache`,
  Linux `$XDG_CACHE_HOME/elegance/glyphs`); `ELG_GLYPH_CACHE` overrides it, and `off` disables it.
- **One file per font**, named by a hash of the font's bytes, so an updated font gets a fresh
  file. The header records the format version and generator settings; a mismatch discards the file.
- **Append-only records** (glyph id, size, pixels, checksum), written by workers. On load, reading
  stops at the first bad record and truncates there, so a torn write loses only what came after it.
- Each file is capped at 64 MB.

## Rules

- The UI thread reserves; workers generate; the render thread uploads.
- Upload pixels before any frame that samples them.
- A cache file is valid only for the exact font bytes and generator settings.

## Edge cases

- **The atlas fills up**: `glyph()` returns null and the glyph doesn't draw. Eviction (LRU) or
  extra pages is the open fix.
- **Glyphs without outlines** (spaces) get no slot.
- **Small sizes** (< ~12 px) look slightly softer than hinted bitmaps. A hinted small-size path
  is a possible addition if measurement shows it's needed.
