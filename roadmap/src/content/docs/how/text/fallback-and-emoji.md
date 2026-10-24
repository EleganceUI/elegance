---
title: Font fallback and color emoji
description: "Choosing a font for characters the requested one lacks, and drawing COLRv0 and COLRv1 color glyphs."
sidebar:
  order: 3
depends: [text/glyphs, graphics/paths]
---

## What it's for

No single font covers every script and emoji. Fallback finds a font for each character the
requested font lacks, lazily and cheaply. Color glyphs (emoji) can't be distance fields; they're
drawn as stacks of colored glyphs or as filled vector regions.

## How it works

### Fallback during shaping

After shaping a paragraph with its font:

1. Find **runs of missing glyphs** (glyph id 0). Glyphs sharing a cluster belong together (an
   emoji and its variation selector), so a run takes all of them.
2. Characters that **join** the previous one (variation selectors, ZWJ, combining marks) stay
   with that character's font rather than choosing their own.
3. Split the run into pieces by the **first fallback font that has each character**, and shape
   each piece with its font. Glyphs from a fallback font carry a pointer to their face.
4. Characters nobody has keep the primary font's missing glyph. Variation selectors and joiners
   nobody draws are removed instead of showing boxes.

The **fallback chain** is per platform. On Windows: Segoe UI Symbol, Segoe UI Emoji, Microsoft
YaHei, Yu Gothic, Malgun Gothic, Nirmala UI, Ebrima, Gadugi, Segoe UI Historic, SimSun, Arial.
Fonts load **one at a time, only when the fonts before them lack a character**, so CJK fonts are
read only when CJK text appears. Lookups are cached per character.

Apps can put their own color emoji font first with `register_emoji_font(path)` (e.g. Twemoji);
characters it lacks still fall back to the system's.

**Emoji presentation**: a character that defaults to emoji presentation, or is followed by
U+FE0F, prefers a **color** font from the chain, even if a text font has a monochrome glyph for
it (a text font's heart for U+2764 U+FE0F). U+FE0E asks for text presentation.

### COLRv0: layered glyphs

A COLRv0 font maps a glyph to a list of **layers**, each a plain glyph plus a palette index. At
load, the face reads the default **CPAL** palette. The first time a color glyph is drawn, its
layers are looked up and cached. `draw_glyph` (the only glyph-drawing path, shared by Text and
text fields) draws a color glyph as its layers bottom to top, each in its palette color.
"Foreground" layers take the text color, and every layer is multiplied by the text's opacity.
Layer glyphs use **single-channel** distance fields, because layers overlap themselves.

### COLRv1: paint graphs

COLRv1 glyphs are a graph of paint operations: layers, glyph outlines, references to other color
glyphs, transforms (translate, scale, rotate, skew, with centers), and fills (solid, linear,
radial, sweep gradients). FreeType exposes the graph. Elegance **flattens it once per glyph** into
a list of **regions**, each a filled polygon set in em units (y down) plus a paint:

- walk the graph with a current transform;
- glyph outlines become polygons (flattened curves) under that transform;
- the fill becomes a `ColorPaint`: a solid color, or a gradient whose geometry is transformed too.

Drawing turns each region into a `PathCommand` through the [path pipeline](../../graphics/paths/).
Emoji stay sharp at any size, and their coverage masks are cached like any path.

Current approximations: two-circle radial gradients become concentric ones around the end
circle; sweep gradients draw as their average color; composite modes draw source-over; extend
modes pad.

## Rules

- Fallback happens during shaping, not drawing, so measurement includes fallback glyphs.
- Load fallback fonts lazily, one at a time.
- One glyph-drawing function for every text view.

## Edge cases

- **Windows 10's Segoe UI Emoji is COLRv0 only.** Test COLRv1 with a Noto COLRv1 font
  (`sandbox --emoji-font`).
- **Bitmap emoji fonts** (CBDT, sbix) aren't supported yet. They'd need an RGBA bitmap atlas per
  size bucket.
- **Big emoji runs** generate one mask per region per size; caching the whole glyph as a layer is
  a possible optimization.
