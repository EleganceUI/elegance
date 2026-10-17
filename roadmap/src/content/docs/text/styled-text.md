---
title: StyledText (rich text)
description: Runs of different fonts and styles wrapped as one text, with links, highlights, and inline attachments.
sidebar:
  order: 4
---

Chat-style rich text: runs with their own weight, family, color, highlight and lines, wrapped as
one paragraph, with clickable runs and inline views. This is newer work (uncommitted at the time
of writing). Build it before [TextField](../text-input/): the field's styled spans and multi-line
mode reuse its layout.

:::note[Depends on]
[Fonts, shaping, and the Text view](../fonts-and-layout/). Clickable runs need
[events](../../framework/input-and-focus/); attachments need any view (e.g. `AsyncImage`).
:::

## Read first

- `RunStyle`, `TextRun`, `StyledText` in `include/elg/text.h`
- `src/text/styled_layout.*`, `src/view/styled_text.cpp`

## Tasks

### Layout
- [ ] `StyledRunSpec { begin, end, face, style, keep_together, box }`: shape each run with its own face, then wrap all of them as one text
- [ ] `StyledShape`: per paragraph, the shaped glyphs and a style index per glyph; paragraph start bytes; the largest ascent and line height of the faces used
- [ ] `keep_together` runs never break across lines (implied by a background)
- [ ] `box > 0`: one placeholder glyph `box` em wide instead of the run's text (for attachments)

### Drawing
- [ ] Per run: color (else the environment's foreground), a rounded highlight background, underline, strikethrough
- [ ] Unset style parts come from the environment (`.font`, `.foreground_style`)

### The view
- [ ] `StyledText(std::vector<TextRun>)`; newlines inside runs start paragraphs
- [ ] `TextRun::on_tap`: hit test runs and make them clickable (links, mentions)
- [ ] `TextRun::attachment`: a view in a square `attachment_size` em box, centered on the line (custom emoji); the run's text stays its accessible label

## Done when

- [ ] A chat line with a highlighted `@mention`, a bold word, inline code in a monospace family, and an inline image wraps correctly at any width
- [ ] Clicking the mention runs its action, and clicking plain text does nothing
