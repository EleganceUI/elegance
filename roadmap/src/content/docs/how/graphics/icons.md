---
title: Icon library (Phosphor)
description: "Icon sets as fonts, the registry and name maps, name resolution, weights, variants and duotone layers."
sidebar:
  order: 2
depends: [text/glyphs]
---

## What it's for

Apps need icons everywhere. Elegance ships an existing icon set (Phosphor, MIT) rather than
drawing its own. Icons render through the text pipeline, so they're as cheap and crisp as glyphs
and take the font size and color of their surroundings. A registry lets apps add any icon font in
one call.

## How it works

### Icons are glyphs

An icon set is one or more **fonts** plus a **name map** (name → codepoint). An icon `Image`
resolves its name to a font family and one or more characters, then draws them like text:

- size from the environment's font size, color from the foreground;
- it sits on the text baseline in HStacks and Labels;
- weight from the environment's font weight, where the set has weights.

Icon fonts get a single-channel distance field (see [MSDF glyphs](../../text/glyphs/)), because
their overlapping outlines break multi-channel fields.

### The registry

`register_icon_set(name, options)`:

- `.font` / `.font_data`: the main font (a file, or bytes embedded in the app);
- `.names` / `.names_data`: the name map;
- `.weights`: fonts per `Weight` (bold, thin, light...);
- `.variants`: named alternatives (`fill`, `duotone`), each a font and optionally its own map;
- `.replace`: allow re-registering a name (otherwise `UsageError`).

Each registration gets **fresh font family names** internally, so replacing a set never mixes its
old and new fonts. The fonts are registered as icon fonts. `set_default_icon_set(name)` picks the
set for unprefixed names. A missing file or unreadable map throws `ResourceError` with the path.

The built-in Phosphor set registers itself through this same call (dogfooding), from fonts
embedded with `elg_embed_file`: regular, bold and fill, plus thin, light and duotone when
`ELG_ICONS_EXTRA_WEIGHTS` is on.

### Name maps, as shipped

The map format is **detected** from the content, so upstream files are used unconverted:

- **CSS** from icon webfonts: `.ph-heart:before { content: "\e2a8"; }`. The class right before
  `:before` names the icon, and the prefix every name shares (`ph-`, `fa-`) is dropped. An
  `:after` rule adds a **second layer**, and `opacity: 0.2` dims a layer (two-tone icons).
- **Flat JSON**: `{"heart": "e2a8"}` or `{"heart": 58024}`.
- **`.codepoints` text** (Material): `heart e2a8` per line.

### Resolving a name

`"<set>.<icon>[.<variant>]"`:

1. If the text before the first dot names a registered set, that's the set; otherwise the whole
   string is looked up in the default set.
2. If the last segment names a variant the set has (`fill`, `duotone`, ...), it selects that
   variant's font and map.
3. The icon name is kept verbatim from upstream (kebab-case), so names copy straight from the
   set's website.
4. Without a variant, the environment's font weight picks a weight font if the set has one.

An unknown name throws `UsageError` listing the **three nearest names** by edit distance, e.g.
`Image("hart"): the icon set "phosphor" has no icon "hart"; did you mean "phosphor.heart", ...?`

### Duotone

A duotone icon has two layers from duotone's own map: the `:before` character at 20% opacity
and the `:after` character on top, drawn as a ZStack in the same color.

### Images and labels

`Image("heart")` and `Image({.system_name = "phosphor.heart.fill"})` are icon images. `Label(title,
{.system_name})` is an icon plus text, spaced and baseline-aligned. `MenuItemOptions::image` puts
icons in menus. Built-in controls (checkbox ✓, chevrons) use Phosphor too, so they look the same
on every platform.

## Rules

- Icons and maps come verbatim from upstream; nothing is redrawn or hand-mapped.
- Built-in sets use the public registration API.
- Unknown names fail loudly with suggestions.

## Edge cases

- **Size**: the six Phosphor styles are ~3 MB embedded. `ELG_ICONS=OFF` drops the set, and the
  extra weights are a separate option.
- **Fonts without a map** would need glyph names from the `post` table; not supported yet.
- **SVG-only sets** wait for an SVG loader on top of [GPU paths](../paths/).
