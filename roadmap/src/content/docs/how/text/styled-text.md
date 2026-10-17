---
title: StyledText (rich text)
description: "Shaping runs with different fonts as one text, wrapping them together, and drawing highlights, lines, links and attachments."
sidebar:
  order: 4
depends: [text/fonts-and-layout, framework/input-and-focus]
---

## What it's for

Chat messages, documentation and editors need text where parts differ: a bold word, an inline
code span in a monospace font, a highlighted mention you can click, an inline custom emoji. All of
it must wrap as **one** paragraph, not as separate views side by side.

## How it works

### Runs

`StyledText({runs...})` takes `TextRun`s: text, a `RunStyle` (weight, italic, family, color,
background, underline, strikethrough, keep-together), an optional `on_tap`, and an optional
`attachment` view with its size in em. Unset style parts come from the environment.

Internally each run becomes a `StyledRunSpec { begin, end, face, style, keep_together, box }`
over the concatenated text:

- `face` is resolved from the environment's font plus the run's weight, italic and family;
- `style` indexes a draw-style table (color, background, lines);
- `box > 0` replaces the run's text with **one placeholder glyph** `box` em wide, the space an
  attachment will occupy.

### Shaping and wrapping as one

`shape_styled(text, runs)` shapes run by run with each run's face (and fallback), then
concatenates per paragraph into one `ShapedParagraph`, recording a **style index per glyph**. The
result (`StyledShape`) also records each paragraph's start byte and the **largest ascent and line
height** of all faces used, so mixed sizes share a baseline grid.

Line breaking is the ordinary [text layout](../fonts-and-layout/) over that combined paragraph,
with one addition: glyphs of `keep_together` runs have no break opportunities between them.
A background implies keep-together, so a highlighted mention never splits across lines.

### Drawing

For each line, consecutive glyphs with the same style form spans:

1. **Backgrounds and lines** first, per stretch of one style: a rounded rect behind the span's
   extent, and underline and strikethrough as thin rects at font-relative offsets.
2. **Glyphs** on top, in the run's color, or the environment's foreground when unset.

### Attachments and taps

An attachment's view is mounted as a child element and placed, each layout, centered on its
placeholder glyph's box: `attachment_size` em square, centered on the line. The run's text stays
in the accessible label, so screen readers say ":blob:" rather than nothing.

Hit testing checks attachments first (an attachment may have its own tooltip or handlers), then
runs with `on_tap` by the glyph boxes of their spans. A click inside one runs its action;
elsewhere the text isn't interactive.

## Rules

- One paragraph model for all runs; styles only change how glyphs are shaped and drawn.
- Keep-together runs never break.
- Attachments occupy exactly their placeholder's space; they don't affect line height beyond the
  line box.

## Edge cases

- **Runs spanning newlines** start new paragraphs mid-run; style indices continue across.
- **A family that isn't registered** fails like any missing font (`ResourceError`); a run's font
  is `Font::custom(family, size, weight)` from the base font.
- **A view exiting** paints without re-layout, so the lines kept must always match the current
  shape (the same rule as Text).

The same shaping and drawing code powers [TextField's styled spans](../text-input/).
