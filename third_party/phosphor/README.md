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
| `phosphor.css` | `src/regular/style.css` (the name -> codepoint map; identical for every weight) |

Elegence embeds these into the library and registers them as the `phosphor` icon set
(design.md §13.2) through the public `register_icon_set` API.
