---
title: Fonts, shaping, and the Text view
description: "Font loading and resolution, shaping with HarfBuzz, line breaking and truncation, and how Text measures and paints."
sidebar:
  order: 1
depends: [framework/environment-and-styles]
---

## What it's for

Text is most of most UIs. This part turns a string and a `Font` into positioned glyphs: which
font file, which glyphs (after ligatures, kerning and complex-script shaping), where lines break,
and where the ellipsis goes. It produces glyph positions; [the next spec](../glyphs/) turns
glyphs into pixels.

## How it works

### Fonts

A `Font` is a description: a family (system, or a registered name), a size in points, a weight,
italic, and a design. Semantic styles (`large_title`, `title`, `headline`, `body`, `caption`, ...)
are predefined descriptions meant to scale with a global text-size setting.

`FontLibrary` (UI thread) resolves a description to a `FontFace`, loading on first use and
caching. A face is one font file: its bytes (shared, read in **one bulk read**), a FreeType face
for outlines and metrics, and a HarfBuzz font for shaping. Metrics (ascent, descent, line gap,
advances) are in **em units**, so one face serves every size. `register_font(family, path |
bytes)` adds app fonts, including fonts embedded in the binary. A missing font throws
`ResourceError`.

### Shaping

Text is split at `\n` into **paragraphs**, and each is shaped once with HarfBuzz into a
`ShapedParagraph`: glyph ids, advances and offsets (em units), and each glyph's **cluster**, the
byte offset of the first character it came from. Clusters are how every later step (breaking,
carets, selection, hit testing) maps glyphs back to text.

Shaping happens when the content or font changes, never per frame or per width. Characters the
face lacks go through [font fallback](../fallback-and-emoji/) during shaping, and those glyphs
carry their own face.

### Line layout

`layout_text(paragraphs, params)` produces lines for a width:

1. **Break opportunities**: after whitespace, and between any two CJK characters.
2. **Greedy wrapping**: add glyphs to the line until the next word doesn't fit, then break at the
   last opportunity. A word wider than the whole line breaks inside itself. Wrapped lines don't
   start with the whitespace they broke at, and trailing whitespace doesn't count toward width.
3. **Line count**: the minimum of `line_limit` and how many lines fit the proposed height (at
   least one).
4. **Truncation**: if text remains after the last allowed line, drop glyphs from that line's end
   until "…" (shaped in the same face) fits, then append it.
5. **Alignment**: offset each line by leading, center or trailing within the width.

A small epsilon absorbs float error, so text measured at width W also fits when laid out at
exactly W.

### The Text element

`TextElement` holds the shaped paragraphs, font, color and the last layout.

- **Measure** for a proposal: with no width, the unwrapped width; with a width, the wrapped size
  for it, capped by the line limit and proposed height. Its minimum is one line plus an ellipsis,
  and its maximum is the full wrapped height, so text competes for space in stacks like any
  flexible view.
- **Arrange** reruns line breaking only when the width or the content changed.
- **Paint**: each glyph becomes a `GlyphCommand`, with its quad from the atlas's metrics × font
  size, positioned on a baseline **snapped to whole device pixels** to keep text crisp. Color
  glyphs paint as layers or paths.
- Content changes reshape, invalidate layout and snap the next frame change (glyphs can't morph);
  color changes only repaint.
- Lines always index the paragraphs they were made from. A view exiting through a transition
  paints without re-layout, and old lines over new, shorter text would read out of range.

## Rules

- Shape once per content or font change; break lines once per width change.
- Everything indexes text by **byte offsets on cluster boundaries**.
- Metrics in em units; points only when placing.

## Edge cases

- **Clusters vs graphemes**: HarfBuzz clusters approximate grapheme boundaries. ICU or UAX #29
  would be exact.
- **Bidi and complex line breaking** aren't done: right-to-left runs aren't reordered, and line
  breaks follow the simple rules above rather than UAX #14.
