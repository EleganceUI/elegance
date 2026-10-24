---
title: Icon library (Phosphor)
description: Icon sets as fonts, a registry, name maps, variants and weights.
sidebar:
  order: 2
---

Icons are glyphs from an upstream icon font, drawn through the text pipeline. Sets live in a
registry; the built-in Phosphor set registers itself through the same public call apps use.

:::note[Depends on]
[MSDF glyphs, the atlas, and the disk cache](../../text/glyphs/) (single-channel fields for icon fonts)
and `elg_embed_file` from [Build](../../foundations/build/).
:::

## Read first

- `design.md` §13.2 (Built-in icon library), M5 icon items
- `third_party/phosphor/` (vendored verbatim with its license), `Image` / `register_icon_set` in `include/elg/image.h`, `src/view/image.cpp`, `tests/image_tests.cpp`

## Tasks

### Registry
- [ ] `register_icon_set(name, {.font | .font_data, .names | .names_data, .variants, .weights, .replace})`; `set_default_icon_set(name)`
- [ ] Registering a name twice is a `UsageError` unless `.replace`; a missing file or unreadable map is a `ResourceError` naming the path
- [ ] Mark registered fonts as icon fonts (single-channel distance fields)

### Name maps (auto-detected)
- [ ] CSS from icon webfonts (`.ph-heart:before { content: "\e2a8"; }`), with the shared class prefix removed
- [ ] Flat JSON (`{"name": "e900"}` or numbers)
- [ ] Material `.codepoints` text
- [ ] Variants can have their own maps (`IconVariant`)

### Using icons
- [ ] `Image({.system_name = "phosphor.heart.fill"})` and `Image("heart")`. The text before the first dot is the set if registered, else the default set; a trailing known variant selects it
- [ ] Unknown names throw `UsageError` with the three nearest names
- [ ] Icons take the environment's font size, weight (bold uses the bold font; thin and light pick theirs) and foreground; they sit on the text baseline
- [ ] Duotone: two layers from duotone's own map, `:before` at 20% opacity and `:after`, drawn as a ZStack
- [ ] `Label(title, {.system_name = ...})`, `MenuItemOptions::image`

### Built-in set
- [ ] Phosphor 2.1.2 (MIT): regular, bold and fill, embedded; `ELG_ICONS=OFF` leaves it out
- [ ] Thin, light and duotone behind `ELG_ICONS_EXTRA_WEIGHTS` (~1.6 MB more)
- [ ] Built-in check marks and chevrons use Phosphor; caption buttons keep Segoe MDL2 on Windows

## Done when

- [ ] `Image("heart").font_weight(Weight::bold).foreground_style(Color::red)` draws a bold red heart at any size
- [ ] A misspelled icon name fails with useful suggestions
- [ ] Each name-map format has a test

## Gotchas

- Don't generate or redraw icons: every glyph comes verbatim from upstream, and the name table comes
  mechanically from the project's own metadata.
- Not built yet: glyph-name maps from the font's `post` table; IcoMoon `selection.json` and
  Fontello configs; the `elegance_icon_set` CMake helper; generated `Icons::phosphor::...`
  constants; making the extra weights opt-in; `.resizable()` for icons; SVG icon sets.
