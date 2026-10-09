# Phosphor Icons (vendored)

- Upstream: https://phosphoricons.com, https://github.com/phosphor-icons/web
- Package: `@phosphor-icons/web` 2.1.2 (npm), unmodified
- License: MIT (see `LICENSE`)

Files, copied verbatim from the package:

| File | From |
|---|---|
| `Phosphor.ttf` | `src/regular/Phosphor.ttf` |
| `Phosphor-Bold.ttf` | `src/bold/Phosphor-Bold.ttf` |
| `Phosphor-Fill.ttf` | `src/fill/Phosphor-Fill.ttf` |
| `Phosphor-Thin.ttf` | `src/thin/Phosphor-Thin.ttf` |
| `Phosphor-Light.ttf` | `src/light/Phosphor-Light.ttf` |
| `Phosphor-Duotone.ttf` | `src/duotone/Phosphor-Duotone.ttf` |
| `phosphor.css` | `src/regular/style.css` (the name -> codepoint map; identical for thin, light, regular, bold, fill) |
| `phosphor-duotone.css` | `src/duotone/style.css` (duotone's own map: two layers per icon, the back one at 20% opacity) |

Elegence embeds these into the library and registers them as the `phosphor` icon set
(design.md §13.2) through the public `register_icon_set` API.
