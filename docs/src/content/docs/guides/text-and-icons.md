---
title: Text, fonts, and icons
description: "Showing text, choosing fonts and weights, registering custom fonts, and using the built-in icon set."
---

## Text

`Text` shows a run of text in one font and color:

```cpp
Text("Hello")
Text([=] { return std::format("{} unread", unread()); }) // reactive
```

It wraps at word boundaries to the width it's offered, and truncates with an ellipsis when it runs
out of lines, either because of `line_limit` or because the height offered runs out.

```cpp
Text(article.body).line_limit(3);
Text(caption).multiline_text_alignment(TextAlignment::center);
```

Text is shaped by HarfBuzz, so ligatures, kerning and combining marks work. Characters missing
from the font fall back to system fonts (CJK, Indic, symbols), and emoji are drawn in color.
Bidirectional text (mixing right-to-left and left-to-right) and script-aware line breaking aren't
supported yet.

## Fonts

A `Font` is a family, a size in points and a weight. Use the semantic text styles for consistent
typography:

| Style | Size | Weight |
| --- | --- | --- |
| `Font::large_title` | 30 | bold |
| `Font::title` | 24 | semibold |
| `Font::title2` | 20 | semibold |
| `Font::title3` | 17 | semibold |
| `Font::headline` | 15 | semibold |
| `Font::body` | 15 | regular (the default) |
| `Font::callout` | 14 | regular |
| `Font::subheadline` | 13 | regular |
| `Font::footnote` | 12 | regular |
| `Font::caption` | 12 | regular |
| `Font::caption2` | 11 | regular |

Or make one:

```cpp
Font::system(17)                       // the platform UI font (Segoe UI on Windows)
Font::system(32, Weight::bold)
Font::title.weight(Weight::regular)
Font::body.bold()
Font::body.with_size(18)
```

Set a font on a `Text`, or for everything inside a view through the environment:

```cpp
Text("Title").font(Font::title).bold();

VStack(Text("A"), Text("B")).font(Font::callout);       // both texts
VStack(Text("A"), Image("gear")).font_weight(Weight::bold); // keeps each one's size
```

The text color comes from `.foreground_style()`, also inherited:

```cpp
Text("Details").foreground_style(ThemeColor::secondary);
```

### Custom fonts

Register a font file, or font data embedded in the app, under a family name. Register each
weight's file separately; text uses the closest registered weight.

```cpp
register_font("Inter", "fonts/Inter-Regular.ttf");
register_font("Inter", "fonts/Inter-SemiBold.ttf", Weight::semibold);
register_font("Brand", embedded_font_bytes);          // std::span<const unsigned char>

Text("Hello").font(Font::custom("Inter", 16, Weight::semibold));
```

Using a family that isn't registered throws `elg::ResourceError` when the text is shown.

## Icons

Elegance embeds [Phosphor](https://phosphoricons.com) (MIT licensed), about 1,500 icons, as its
default icon set. Icons are glyphs: they take the font size, weight and foreground color of their
environment, and sit on the text baseline next to text.

```cpp
Image("gear")
Image("heart").foreground_style(ThemeColor::danger).font(Font::title2)
Image("bell").font_weight(Weight::bold)     // Phosphor's bold style
Image("heart.fill")                         // a variant: fill
Image("phosphor.star.duotone")              // set, icon, variant
```

Names are Phosphor's own; browse them at [phosphoricons.com](https://phosphoricons.com). An
unknown name throws `elg::UsageError` listing the closest matches. Phosphor's weights map to
`.font_weight()`: thin, light, regular and bold, plus the `fill` and `duotone` variants.

`Label` puts an icon in front of a title, spaced for buttons, menus and lists:

```cpp
Button(Label("Record", {.system_name = "microphone"}), record);
```

Icon-only buttons need an accessibility label, since a glyph says nothing to a screen reader:

```cpp
Button(Image("trash"), remove).accessibility_label("Delete");
```

### Your own icon sets

Any icon font with a name map can be registered as a set: CSS from icon webfonts, a JSON map, or
Material-style codepoint files.

```cpp
register_icon_set("brand", {.font = "brand-icons.ttf", .names = "brand-icons.css"});
Image("brand.logo");

set_default_icon_set("brand"); // names without a prefix now come from "brand"
```

Sets can have several weights (`.weights`) and variants (`.variants`). See
[`<elg/image.h>`](../../reference/image/) for the options.
