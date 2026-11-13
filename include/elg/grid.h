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

/// How a VGrid column (or HGrid row) is sized.
struct GridItem {
    /// How the track takes its size.
    enum class Kind {
        fixed,    ///< exactly `minimum` points
        flexible, ///< a share of the space, within [minimum, maximum]
        adaptive, ///< as many tracks as fit in its share, each within [minimum, maximum]
    };

    Kind kind = Kind::flexible; ///< How the track takes its size.
    /// The smallest size in points: the size of a fixed track, and of a flexible or adaptive one when
    /// the grid has no length to share (its ideal size).
    float minimum = 10;
    float maximum = infinity; ///< The largest size in points, for flexible and adaptive tracks.
    /// Space after this track (between its tracks too, for adaptive). Default: the grid's spacing.
    OptionalLength spacing{};
    /// Where views sit in this track's cells. Default: the grid's alignment.
    std::optional<Alignment> alignment{};

    /// A track of exactly `size` points.
    ///
    /// @param size The track's size in points.
    /// @return The item.
    static constexpr GridItem fixed(float size) noexcept { return {Kind::fixed, size, size}; }
    /// A track that takes an equal share of the space left by fixed tracks, within its limits.
    ///
    /// @param minimum The smallest size in points.
    /// @param maximum The largest size in points.
    /// @return The item.
    static constexpr GridItem flexible(float minimum = 10, float maximum = infinity) noexcept {
        return {Kind::flexible, minimum, maximum};
    }
    /// As many tracks as fit in an equal share of the space, each at least `minimum` points (one
    /// squeezed track if even one doesn't fit). Without a length to share, one track of `minimum`.
    ///
    /// @param minimum The smallest size of each track in points.
    /// @param maximum The largest size of each track in points.
    /// @return The item.
    static constexpr GridItem adaptive(float minimum, float maximum = infinity) noexcept {
        return {Kind::adaptive, minimum, maximum};
    }

    /// @param length The space after this track in points; unset: the grid's spacing.
    /// @return A copy of this item with `spacing` set to `length`.
    [[nodiscard]] constexpr GridItem with_spacing(OptionalLength length) const noexcept {
        GridItem item = *this;
        item.spacing = length;
        return item;
    }
    /// @param cell_alignment Where views sit in this track's cells.
    /// @return A copy of this item with `alignment` set to `cell_alignment`.
    [[nodiscard]] constexpr GridItem with_alignment(Alignment cell_alignment) const noexcept {
        GridItem item = *this;
        item.alignment = cell_alignment;
        return item;
    }
};

/// One resolved track: its size, the space after it, and its cells' alignment (if set).
struct GridTrack {
    float size = 0;                     ///< Size across the track, in points.
    float spacing = 0;                  ///< Space after the track, in points (unused after the last).
    std::optional<Alignment> alignment; ///< Where views sit in its cells; unset: the grid's alignment.
};

/// The tracks `items` make in `length` points. Fixed tracks take their size; the rest share what's
/// left equally, within their limits; an adaptive item becomes as many tracks as fit in its share.
/// At least one track results.
///
/// @param items The track descriptions; empty counts as one GridItem::flexible().
/// @param length The space to fill in points; nullopt or infinity: the tracks' ideal sizes.
/// @param spacing The space after items without their own spacing (default: default_stack_spacing).
/// @return The tracks, in order.
ELG_API std::vector<GridTrack> resolve_grid_tracks(std::span<const GridItem> items, std::optional<float> length,
                                                   OptionalLength spacing = {});

/// The space all of `tracks` take, with the spacing between them.
///
/// @param tracks Tracks from resolve_grid_tracks().
/// @return The total length in points; 0 for no tracks.
ELG_API float grid_tracks_length(std::span<const GridTrack> tracks) noexcept;

/// A VGrid's layout: children fill the columns of a row, then the next row. Rows are as tall as
/// their tallest cell. Columns narrower than the grid sit at `alignment`'s horizontal edge.
class ELG_API VGridLayout final : public Layout {
public:
    /// @param columns The columns; empty counts as one GridItem::flexible().
    /// @param alignment Where views sit in their cells, and where the columns sit when narrower than
    ///   the grid.
    /// @param spacing Between rows, and between columns without their own; unset:
    ///   default_stack_spacing.
    explicit VGridLayout(std::vector<GridItem> columns = {GridItem::flexible()}, Alignment alignment = Alignment::center,
                         OptionalLength spacing = {})
        : columns_(std::move(columns)), alignment_(alignment), spacing_(spacing) {}

    /// Measures the grid: its columns resolved for the proposed width, by its rows' heights.
    ///
    /// @param proposal The size the parent offers: per dimension, nullopt asks for the ideal size, 0 for
    ///   the minimum and infinity for the maximum.
    /// @param children The cells, row after row.
    /// @return The grid's size.
    [[nodiscard]] Size size_that_fits(ProposedSize proposal, std::span<const LayoutChild> children) const override;
    /// Places the cells row after row, each child offered its cell's size.
    ///
    /// @param bounds Has the size reported for `proposal`, positioned by the parent.
    /// @param proposal The proposal that size was measured for; unused.
    /// @param children The cells, row after row.
    void place_children(Rect bounds, ProposedSize proposal, std::span<const LayoutChild> children) const override;

private:
    ELG_PRIVATE_MEMBERS_BEGIN
    std::vector<GridItem> columns_;
    Alignment alignment_;
    std::optional<float> spacing_; // between rows, and columns without their own
    ELG_PRIVATE_MEMBERS_END
};

/// An HGrid's layout: children fill the rows of a column, then the next column. Columns are as wide
/// as their widest cell. Rows shorter than the grid sit at `alignment`'s vertical edge.
class ELG_API HGridLayout final : public Layout {
public:
    /// @param rows The rows; empty counts as one GridItem::flexible().
    /// @param alignment Where views sit in their cells, and where the rows sit when shorter than the
    ///   grid.
    /// @param spacing Between columns, and between rows without their own; unset:
    ///   default_stack_spacing.
    explicit HGridLayout(std::vector<GridItem> rows = {GridItem::flexible()}, Alignment alignment = Alignment::center,
                         OptionalLength spacing = {})
        : rows_(std::move(rows)), alignment_(alignment), spacing_(spacing) {}

    /// Measures the grid: its rows resolved for the proposed height, by its columns' widths.
    ///
    /// @param proposal The size the parent offers: per dimension, nullopt asks for the ideal size, 0 for
    ///   the minimum and infinity for the maximum.
    /// @param children The cells, column after column.
    /// @return The grid's size.
    [[nodiscard]] Size size_that_fits(ProposedSize proposal, std::span<const LayoutChild> children) const override;
    /// Places the cells column after column, each child offered its cell's size.
    ///
    /// @param bounds Has the size reported for `proposal`, positioned by the parent.
    /// @param proposal The proposal that size was measured for; unused.
    /// @param children The cells, column after column.
    void place_children(Rect bounds, ProposedSize proposal, std::span<const LayoutChild> children) const override;

private:
    ELG_PRIVATE_MEMBERS_BEGIN
    std::vector<GridItem> rows_;
    Alignment alignment_;
    std::optional<float> spacing_; // between columns, and rows without their own
    ELG_PRIVATE_MEMBERS_END
};

/// A Grid's layout: GridRow cells in columns as wide as their widest cell, rows as tall as their
/// tallest. When the grid is offered more width than its columns want, columns with flexible
/// cells (a TextField, a frame with max_width = infinity) share the rest. When it's offered less,
/// every column gives up width in proportion (text wraps).
class ELG_API GridLayout final : public Layout {
public:
    /// @param alignment Where views sit in their cells, and where the columns sit when narrower than
    ///   the grid.
    /// @param horizontal_spacing Between columns; unset: default_stack_spacing.
    /// @param vertical_spacing Between rows; unset: default_stack_spacing.
    explicit GridLayout(Alignment alignment = Alignment::center, OptionalLength horizontal_spacing = {},
                        OptionalLength vertical_spacing = {}) noexcept
        : alignment_(alignment), horizontal_spacing_(horizontal_spacing), vertical_spacing_(vertical_spacing) {}

    /// Measures the table.
    ///
    /// @param proposal The size the parent offers: per dimension, nullopt asks for the ideal size, 0 for
    ///   the minimum and infinity for the maximum.
    /// @param children The cells of every GridRow, and the full-width children between them.
    /// @return The grid's size.
    [[nodiscard]] Size size_that_fits(ProposedSize proposal, std::span<const LayoutChild> children) const override;
    /// Places the cells in their columns and rows.
    ///
    /// @param bounds Has the size reported for `proposal`, positioned by the parent.
    /// @param proposal The proposal that size was measured for; unused.
    /// @param children The cells of every GridRow, and the full-width children between them.
    void place_children(Rect bounds, ProposedSize proposal, std::span<const LayoutChild> children) const override;

private:
    Alignment alignment_;
    ELG_PRIVATE_MEMBERS_BEGIN
    OptionalLength horizontal_spacing_;
    OptionalLength vertical_spacing_;
    ELG_PRIVATE_MEMBERS_END
};

/// Options for VGrid().
struct VGridOptions {
    std::vector<GridItem> columns = {GridItem::flexible()}; ///< The columns; default: one flexible column.
    Alignment alignment = Alignment::center; ///< of views in their cells, and of the columns
    /// Between rows, and between columns that don't set their own; default: default_stack_spacing.
    OptionalLength spacing;
};

/// Options for HGrid().
struct HGridOptions {
    std::vector<GridItem> rows = {GridItem::flexible()}; ///< The rows; default: one flexible row.
    Alignment alignment = Alignment::center;               ///< of views in their cells, and of the rows
    /// between columns, and between rows that don't set their own; default: default_stack_spacing
    OptionalLength spacing;
};

/// Options for Grid().
struct GridOptions {
    Alignment alignment = Alignment::center; ///< of views in their cells
    OptionalLength horizontal_spacing;       ///< between columns; default: default_stack_spacing
    OptionalLength vertical_spacing;         ///< between rows; default: default_stack_spacing
};

/// Options for GridRow().
struct GridRowOptions {
    std::optional<VerticalAlignment> alignment; ///< of this row's cells; default: the grid's
};

/// Children in columns, row after row (ForEach items are cells too).
///
/// @param options The columns, alignment and spacing.
/// @param children The cells.
/// @return The grid.
template <std::convertible_to<View>... Children>
View VGrid(VGridOptions options, Children&&... children) {
    return VGridLayout(std::move(options.columns), options.alignment, options.spacing)(std::forward<Children>(children)...);
}

/// Children in rows, column after column.
///
/// @param options The rows, alignment and spacing.
/// @param children The cells.
/// @return The grid.
template <std::convertible_to<View>... Children>
View HGrid(HGridOptions options, Children&&... children) {
    return HGridLayout(std::move(options.rows), options.alignment, options.spacing)(std::forward<Children>(children)...);
}

/// A table of GridRows; other children take a whole row.
///
/// @param options The alignment and spacing.
/// @param children GridRows, and full-width children such as a Separator.
/// @return The grid.
template <std::convertible_to<View>... Children>
View Grid(GridOptions options, Children&&... children) {
    return GridLayout(options.alignment, options.horizontal_spacing, options.vertical_spacing)(
        std::forward<Children>(children)...);
}

/// Like Grid(GridOptions, Children&&...), with the default options.
///
/// @param children GridRows, and full-width children such as a Separator.
/// @return The grid.
template <std::convertible_to<View>... Children>
View Grid(Children&&... children) {
    return Grid(GridOptions{}, std::forward<Children>(children)...);
}

namespace detail {
ELG_API View make_grid_row(GridRowOptions options, std::vector<View> cells);

} // namespace detail

/// One row of a Grid: each child is a cell. Use ForEach to repeat rows, not cells.
///
/// @param options The row's vertical alignment.
/// @param cells The cells, one per column (or more with View::grid_cell_columns).
/// @return The row.
template <std::convertible_to<View>... Cells>
View GridRow(GridRowOptions options, Cells&&... cells) {
    return detail::make_grid_row(options, {View(std::forward<Cells>(cells))...});
}

/// Like GridRow(GridRowOptions, Cells&&...), with the default options.
///
/// @param cells The cells, one per column (or more with View::grid_cell_columns).
/// @return The row.
template <std::convertible_to<View>... Cells>
View GridRow(Cells&&... cells) {
    return GridRow(GridRowOptions{}, std::forward<Cells>(cells)...);
}

} // namespace elg

#endif // ELG_GRID_H
