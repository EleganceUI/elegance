// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_GRID_H
#define ELG_GRID_H

// Grids: views in columns and rows.
//
// VGrid / HGrid flow their children into tracks (columns of a VGrid, rows of an HGrid), filling
// one line before the next. Tracks are fixed, flexible, or adaptive (as many as fit):
//
//   VGrid({.columns = {GridItem::adaptive(160)}, .spacing = 16}, ForEach(mods, &Mod::id, ModCard))
//   VGrid({.columns = {GridItem::fixed(48), GridItem::flexible()}}, icon1, name1, icon2, name2)
//
// Grid is a table: its children are GridRows, and each column is as wide as its widest cell.
// Other children (a Separator, a heading) take a row of their own, as wide as the grid.
//
//   Grid(GridRow(Text("Name"), Text("Size")),
//        Separator(),
//        GridRow(Text("Skyrim"), Text("12 GB").grid_column_alignment(HorizontalAlignment::trailing)),
//        GridRow(Text("A note that spans both columns").grid_cell_columns(2)))
//
// Cells are modified with View::grid_cell_columns, grid_cell_alignment, and grid_column_alignment.

#include <elg/export.h>
#include <elg/layout.h>
#include <elg/view.h>

#include <concepts>

#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace elg {

// How a VGrid column (or HGrid row) is sized.
struct GridItem {
    enum class Kind {
        fixed,    // exactly `minimum` points
        flexible, // a share of the space, within [minimum, maximum]
        adaptive, // as many tracks as fit in its share, each within [minimum, maximum]
    };

    Kind kind = Kind::flexible;
    float minimum = 10;
    float maximum = infinity;
    // Space after this track (between its tracks too, for adaptive). Default: the grid's spacing.
    OptionalLength spacing{};
    // Where views sit in this track's cells. Default: the grid's alignment.
    std::optional<Alignment> alignment{};

    static constexpr GridItem fixed(float size) noexcept { return {Kind::fixed, size, size}; }
    static constexpr GridItem flexible(float minimum = 10, float maximum = infinity) noexcept {
        return {Kind::flexible, minimum, maximum};
    }
    static constexpr GridItem adaptive(float minimum, float maximum = infinity) noexcept {
        return {Kind::adaptive, minimum, maximum};
    }

    constexpr GridItem with_spacing(OptionalLength length) const noexcept {
        GridItem item = *this;
        item.spacing = length;
        return item;
    }
    constexpr GridItem with_alignment(Alignment cell_alignment) const noexcept {
        GridItem item = *this;
        item.alignment = cell_alignment;
        return item;
    }
};

// One resolved track: its size, the space after it, and its cells' alignment (if set).
struct GridTrack {
    float size = 0;
    float spacing = 0;
    std::optional<Alignment> alignment;
};

// The tracks `items` make in `length` points (nullopt or infinity: their ideal sizes). Fixed
// tracks take their size; the rest share what's left equally, within their limits; an adaptive
// item becomes as many tracks as fit in its share. At least one track results. Items without
// their own spacing are `spacing` apart (default: default_stack_spacing).
ELG_API std::vector<GridTrack> resolve_grid_tracks(std::span<const GridItem> items, std::optional<float> length,
                                                   OptionalLength spacing = {});

// The space all of `tracks` take, with the spacing between them.
ELG_API float grid_tracks_length(std::span<const GridTrack> tracks) noexcept;

// A VGrid's layout: children fill the columns of a row, then the next row. Rows are as tall as
// their tallest cell. Columns narrower than the grid sit at `alignment`'s horizontal edge.
class ELG_API VGridLayout final : public Layout {
public:
    explicit VGridLayout(std::vector<GridItem> columns = {GridItem::flexible()}, Alignment alignment = Alignment::center,
                         OptionalLength spacing = {})
        : columns_(std::move(columns)), alignment_(alignment), spacing_(spacing) {}

    Size size_that_fits(ProposedSize proposal, std::span<const LayoutChild> children) const override;
    void place_children(Rect bounds, ProposedSize proposal, std::span<const LayoutChild> children) const override;

private:
    ELG_PRIVATE_MEMBERS_BEGIN
    std::vector<GridItem> columns_;
    Alignment alignment_;
    std::optional<float> spacing_; // between rows, and columns without their own
    ELG_PRIVATE_MEMBERS_END
};

// An HGrid's layout: children fill the rows of a column, then the next column.
class ELG_API HGridLayout final : public Layout {
public:
    explicit HGridLayout(std::vector<GridItem> rows = {GridItem::flexible()}, Alignment alignment = Alignment::center,
                         OptionalLength spacing = {})
        : rows_(std::move(rows)), alignment_(alignment), spacing_(spacing) {}

    Size size_that_fits(ProposedSize proposal, std::span<const LayoutChild> children) const override;
    void place_children(Rect bounds, ProposedSize proposal, std::span<const LayoutChild> children) const override;

private:
    ELG_PRIVATE_MEMBERS_BEGIN
    std::vector<GridItem> rows_;
    Alignment alignment_;
    std::optional<float> spacing_; // between columns, and rows without their own
    ELG_PRIVATE_MEMBERS_END
};

// A Grid's layout: GridRow cells in columns as wide as their widest cell, rows as tall as their
// tallest. When the grid is offered more width than its columns want, columns with flexible
// cells (a TextField, a frame with max_width = infinity) share the rest.
class ELG_API GridLayout final : public Layout {
public:
    explicit GridLayout(Alignment alignment = Alignment::center, OptionalLength horizontal_spacing = {},
                        OptionalLength vertical_spacing = {}) noexcept
        : alignment_(alignment), horizontal_spacing_(horizontal_spacing), vertical_spacing_(vertical_spacing) {}

    Size size_that_fits(ProposedSize proposal, std::span<const LayoutChild> children) const override;
    void place_children(Rect bounds, ProposedSize proposal, std::span<const LayoutChild> children) const override;

private:
    Alignment alignment_;
    ELG_PRIVATE_MEMBERS_BEGIN
    OptionalLength horizontal_spacing_;
    OptionalLength vertical_spacing_;
    ELG_PRIVATE_MEMBERS_END
};

struct VGridOptions {
    std::vector<GridItem> columns = {GridItem::flexible()};
    Alignment alignment = Alignment::center; // of views in their cells, and of the columns
    // Between rows, and between columns that don't set their own; default: default_stack_spacing.
    OptionalLength spacing;
};

struct HGridOptions {
    std::vector<GridItem> rows = {GridItem::flexible()};
    Alignment alignment = Alignment::center;
    OptionalLength spacing; // between columns, and between rows that don't set their own
};

struct GridOptions {
    Alignment alignment = Alignment::center; // of views in their cells
    OptionalLength horizontal_spacing;       // between columns; default: default_stack_spacing
    OptionalLength vertical_spacing;         // between rows
};

struct GridRowOptions {
    std::optional<VerticalAlignment> alignment; // of this row's cells; default: the grid's
};

// Children in columns, row after row (ForEach items are cells too).
template <std::convertible_to<View>... Children>
View VGrid(VGridOptions options, Children&&... children) {
    return VGridLayout(std::move(options.columns), options.alignment, options.spacing)(std::forward<Children>(children)...);
}

// Children in rows, column after column.
template <std::convertible_to<View>... Children>
View HGrid(HGridOptions options, Children&&... children) {
    return HGridLayout(std::move(options.rows), options.alignment, options.spacing)(std::forward<Children>(children)...);
}

// A table of GridRows; other children take a whole row.
template <std::convertible_to<View>... Children>
View Grid(GridOptions options, Children&&... children) {
    return GridLayout(options.alignment, options.horizontal_spacing, options.vertical_spacing)(
        std::forward<Children>(children)...);
}

template <std::convertible_to<View>... Children>
View Grid(Children&&... children) {
    return Grid(GridOptions{}, std::forward<Children>(children)...);
}

namespace detail {
ELG_API View make_grid_row(GridRowOptions options, std::vector<View> cells);

} // namespace detail

// One row of a Grid: each child is a cell. Use ForEach to repeat rows, not cells.
template <std::convertible_to<View>... Cells>
View GridRow(GridRowOptions options, Cells&&... cells) {
    return detail::make_grid_row(options, {View(std::forward<Cells>(cells))...});
}

template <std::convertible_to<View>... Cells>
View GridRow(Cells&&... cells) {
    return GridRow(GridRowOptions{}, std::forward<Cells>(cells)...);
}

} // namespace elg

#endif // ELG_GRID_H
