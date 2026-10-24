---
title: Grids
description: "Views in columns and rows - VGrid and HGrid with fixed, flexible, and adaptive tracks, and Grid tables of GridRows."
---

Elegance has two kinds of grid, both in `<elg/grid.h>`:

- **`VGrid` and `HGrid`** flow their children into tracks, filling one line before starting the
  next: a photo wall, a library of cards, a palette of swatches.
- **`Grid`** is a table: its children are `GridRow`s, and each column is as wide as its widest
  cell. Forms, settings, and data that lines up in columns.

## VGrid and HGrid

A `VGrid` lays its children out in columns, row after row. The columns are `GridItem`s:

```cpp
// As many 150-point columns as fit; they share the width exactly.
VGrid({.columns = {GridItem::adaptive(150)}, .spacing = 12},
      ForEach(games, &Game::id, GameCard));

// An icon column, then a column that takes the rest.
VGrid({.columns = {GridItem::fixed(32), GridItem::flexible()}},
      Image("star"), Text("Favorites"),
      Image("clock"), Text("Recent"));
```

Each `GridItem` is one of:

| Item | Size |
|---|---|
| `GridItem::fixed(size)` | exactly `size` points |
| `GridItem::flexible(min, max)` | an equal share of what fixed items leave, within `[min, max]` |
| `GridItem::adaptive(min, max)` | as many tracks as fit in its share at `min` or wider, each up to `max` |

An adaptive item is what makes a grid responsive: as the window widens it adds columns, and the
columns always fill the width. With less room than one track's minimum it makes a single
narrower track rather than overflowing.

`with_spacing(points)` sets the space after an item (and between an adaptive item's tracks),
overriding the grid's spacing, and `with_alignment(alignment)` where views sit in its cells:

```cpp
VGrid({.columns = {GridItem::fixed(120).with_alignment(Alignment::trailing).with_spacing(16),
                   GridItem::flexible().with_alignment(Alignment::leading)}},
      Text("Name"), TextField("", name),
      Text("Email"), TextField("", email));
```

`VGridOptions::spacing` is the space between rows, and between columns that don't set their own;
`alignment` is the default alignment of views
in their cells (and of the columns, when fixed columns don't fill the grid). Each row is as tall as
its tallest cell, and a cell offers its view the cell's whole size, so a flexible view (a color, a
frame with `max_width = infinity`) fills it.

`HGrid` is the same turned on its side: its `rows` are the tracks, and children fill a column
before the next one. It's usually inside a horizontal `ScrollView`.

## Grid

A `Grid` holds `GridRow`s. Columns line up across rows, each as wide as its widest cell:

```cpp
Grid({.alignment = Alignment::leading, .horizontal_spacing = 24},
     GridRow(Text("Game").font(Font::headline), Text("Mods").font(Font::headline)),
     Divider(),
     ForEach(games, &Game::id, [](const Game& game) {
         return GridRow(Text(game.name), Text(std::format("{}", game.mods)));
     }),
     GridRow(Text("Mods include disabled ones.").grid_cell_columns(2)));
```

- A child that isn't a `GridRow` (the `Divider` above, or a heading) takes a row of its own, as
  wide as the grid.
- Repeat rows with `ForEach` (each item a `GridRow`), as above. A `ForEach` inside a `GridRow`
  is one cell, not several.
- When the grid is offered more width than its columns want, the columns with flexible cells
  share the extra. Offered less, every column narrows in proportion and text wraps.

Cells are adjusted with three modifiers:

| Modifier | Effect |
|---|---|
| `.grid_cell_columns(n)` | the cell spans `n` columns |
| `.grid_column_alignment(alignment)` | the horizontal alignment of every cell in this cell's column (put it on one cell, usually the header) |
| `.grid_cell_alignment(alignment)` | where this view sits in its cell, overriding the row, column, and grid |

`GridRowOptions::alignment` sets a row's vertical alignment, and `GridOptions` the grid's default
alignment and the spacing between columns and rows.

## Custom layouts

`VGridLayout`, `HGridLayout`, and `GridLayout` are the layouts behind these views, and
`resolve_grid_tracks` the track sizing: use them in your own [layouts](../custom-layouts/).
