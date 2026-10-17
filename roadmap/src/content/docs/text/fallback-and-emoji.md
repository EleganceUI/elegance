---
title: Font fallback and color emoji
description: System fallback chains, COLRv0 layers, and COLRv1 paint graphs through the path pipeline.
sidebar:
  order: 3
---

Characters the requested font lacks are reshaped with the first system font that has them.
Color emoji draw as layered glyphs (COLRv0) or as flattened, filled paths (COLRv1).

:::note[Depends on]
[MSDF glyphs, the atlas, and the disk cache](../glyphs/). COLRv1 also needs
[GPU vector paths](../../graphics/paths/), so you can leave it until part 6.
:::

## Read first

- `design.md` "Rendering foundations": font fallback, color emoji COLRv0 and COLRv1
- `FontLibrary::fallback` in `src/text/font_library.*`, `src/text/color_glyph.cpp`, `src/text/glyph_draw.h`

## Tasks

### Fallback
- [ ] Runs with missing glyphs are reshaped with the first system font that has each character; glyphs carry their face
- [ ] Windows chain: Segoe UI Symbol, Segoe UI Emoji, Microsoft YaHei, Yu Gothic, Malgun Gothic, Nirmala UI, Ebrima, Gadugi, Segoe UI Historic, SimSun, Arial. Separate lists for macOS and Linux
- [ ] Load fonts lazily down the chain (a CJK font is read only when CJK text appears), with a per-character cache
- [ ] Variation selectors and joiners nobody draws vanish instead of showing boxes
- [ ] `register_emoji_font(path)`: an app's color emoji font (e.g. Twemoji) tried before the system's for emoji; characters it lacks still fall back. Thread-safe; register before text is first shaped
- [ ] Prefer a color font for emoji: characters with default emoji presentation, or followed by U+FE0F. U+FE0E keeps the text style; ★ ♥ ✓ stay with Segoe UI Symbol

### COLRv0
- [ ] Read the default CPAL palette at load; look up a glyph's COLR layers the first time it's drawn
- [ ] `draw_glyph` is the **one** glyph-drawing path for Text and text fields: draw color glyphs as layers bottom to top, each in its palette color
- [ ] "Foreground" layers take the text color; every layer fades with the text's opacity
- [ ] Layer glyphs use single-channel distance fields (layers overlap themselves)

### COLRv1
- [ ] Read the paint graph through FreeType and flatten it once per glyph into filled regions (outline in em units + a fill)
- [ ] Paints: layers, glyph and color-glyph references, transform, translate, scale, rotate and skew (with centers), solid, linear and radial gradients
- [ ] Draw the regions as `PathCommand`s, so emoji stay sharp at any size and their masks are cached
- [ ] A `--emoji-font` sandbox switch for testing with a COLRv1 font (Windows 10's Segoe UI Emoji is COLRv0 only)

## Done when

- [ ] `"Hello 你好 مرحبا 👋🏽"` renders without boxes (the Arabic will be unshaped left-to-right until bidi exists)
- [ ] An emoji with a skin-tone modifier draws as one glyph
- [ ] Noto COLRv1 emoji render with their gradients

## Gotchas

- Approximations to know about: two-circle radial gradients become concentric ones around the end
  circle; sweep gradients draw as their average color; composite modes draw source-over; extend
  modes pad.
- Not built yet: sweep gradients and two-circle radials in the shader, COLRv1 composite modes and
  clip boxes, CBDT/sbix bitmap emoji, and caching big emoji runs as one layer.
