---
title: Fonts, shaping, and the Text view
description: FreeType and HarfBuzz, the font library, word wrapping, truncation, and Text.
sidebar:
  order: 1
---

Load fonts with FreeType, shape with HarfBuzz, wrap and truncate on the UI thread, then hand
glyph quads to the renderer. This page produces positioned glyphs. The next one makes them
pixels.

:::note[Depends on]
[Layout](../../framework/layout/) and [Environment, themes, and styles](../../framework/environment-and-styles/).
:::

## Read first

- `design.md` §5 (Text rendering), §11, M1 text items
- `include/elg/font.h`, `include/elg/text.h`
- `src/text/font_library.*`, `src/text/text_layout.*`, `src/text/text_system.h`, `src/view/text.cpp`

## Tasks

### Fonts
- [ ] `FontFace`: one font file (shared bytes), a FreeType face for outlines and metrics, a HarfBuzz font for shaping; metrics in em units
- [ ] Read font files in **one bulk read** (see Gotchas)
- [ ] `FontLibrary` (UI thread): resolve a `Font` description to a face; throw `ResourceError` when a font can't be found
- [ ] `Font::system(size, weight, design)`, semantic styles (`large_title`, `title`, `title2`, `headline`, `body`, `caption`...), `Font::custom(family, size)`
- [ ] `register_font(family, path)` and `register_font(family, std::span<const unsigned char>)` (fonts from memory)
- [ ] Weight selection; `View::font_weight(Weight)` changes the inherited font's weight

### Shaping and layout
- [ ] `ShapedParagraph`: split at `\n`, shape each paragraph once at mount with HarfBuzz; advances and offsets in em units; clusters as byte offsets
- [ ] Greedy word wrapping; break inside a word only when it doesn't fit on a line by itself
- [ ] `line_limit` and height truncation with an ellipsis
- [ ] Multi-line alignment (`leading`, `center`, `trailing`) and `line_spacing`
- [ ] Snap baselines to whole pixels

### The Text view
- [ ] `Text(Prop<std::string>)`; content, font and color set by setters (font first)
- [ ] Text competes for space: it can shrink to one line with an ellipsis and grow to its full height
- [ ] Reactive text relayouts; a color change only repaints
- [ ] `Text::bold()`, `italic()`, `line_limit()`, `multiline_text_alignment()`
- [ ] Emit one `GlyphCommand` per glyph (quad in points, atlas uv, color, em size)

## Done when

- [ ] A paragraph wraps correctly when the window is resized, with an ellipsis at `line_limit(2)`
- [ ] Ligatures (`fi` in a font that has them) and kerning come out of HarfBuzz
- [ ] Text at 13 px looks crisp enough at 100% scale (judge by eye against native apps)

## Gotchas

- **Read fonts with one bulk read.** Reading through a stream iterator took about 1 s per CJK font
  in a debug build.
- Byte offsets must land on cluster boundaries everywhere (wrapping, carets, selection).
- Not built yet: bidi, script itemization, UAX #14 line breaking (CJK breaks between characters
  for now), system font enumeration through DirectWrite / CoreText / fontconfig, variable font
  axes, `Text + Text` concatenation, and selectable static text.
