// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// VGrid, HGrid, and Grid (elg/grid.h).

#include <elg/grid.h>

#include <elg/modifier.h>
#include <elg/views.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <numeric>

namespace elg {

namespace {

// What marks a view as a cell of a GridRow.
struct GridRowCell {
    std::uint64_t row = 0; // 0: not in a row
    std::uint32_t index = 0;
    std::optional<VerticalAlignment> alignment;
};

struct GridRowKey {
    using Value = GridRowCell;
    static GridRowCell default_value() { return {}; }
};

struct GridCellColumnsKey {
    using Value = int;
    static int default_value() { return 1; }
};

struct GridCellAlignmentKey {
    using Value = std::optional<Alignment>;
    static std::optional<Alignment> default_value() { return std::nullopt; }
};

struct GridColumnAlignmentKey {
    using Value = std::optional<HorizontalAlignment>;
    static std::optional<HorizontalAlignment> default_value() { return std::nullopt; }
};

float fraction(HorizontalAlignment alignment) {
    switch (alignment) {
    case HorizontalAlignment::leading: return 0;
    case HorizontalAlignment::center: return 0.5f;
    case HorizontalAlignment::trailing: return 1;
    }
    return 0.5f;
}

float fraction(VerticalAlignment alignment) {
    switch (alignment) {
    case VerticalAlignment::top: return 0;
    case VerticalAlignment::center: return 0.5f;
    case VerticalAlignment::bottom: return 1;
    }
    return 0.5f;
}

float spacing_or_default(std::optional<float> spacing) {
    return spacing.value_or(default_stack_spacing);
}

std::optional<float> finite(std::optional<float> length) {
    return length && std::isfinite(*length) ? length : std::nullopt;
}

// Places `child` in `cell` at `alignment`, offering it the cell's size.
void place_in_cell(const LayoutChild& child, Rect cell, Alignment alignment) {
    const ProposedSize proposal{cell.width, cell.height};
    const Size size = child.size_that_fits(proposal);
    child.place(aligned_position(cell, size, alignment), proposal);
}

// --- VGrid and HGrid: one implementation along an axis ---
//
// Tracks lie across `axis` (columns of a VGrid: the horizontal axis); children fill the tracks of
// a line, then the next line along the other axis.

struct LineGrid {
    Axis axis;
    std::span<const GridItem> items;
    Alignment alignment;
    std::optional<float> line_spacing;

    float across(Size size) const { return axis == Axis::horizontal ? size.width : size.height; }
    float along(Size size) const { return axis == Axis::horizontal ? size.height : size.width; }
    std::optional<float> across(ProposedSize proposal) const {
        return axis == Axis::horizontal ? proposal.width : proposal.height;
    }
    ProposedSize proposal(std::optional<float> across_length, std::optional<float> along_length) const {
        return axis == Axis::horizontal ? ProposedSize{across_length, along_length}
                                        : ProposedSize{along_length, across_length};
    }
    Size size(float across_length, float along_length) const {
        return axis == Axis::horizontal ? Size{across_length, along_length} : Size{along_length, across_length};
    }

    // How long each line is along the axis: its longest cell.
    std::vector<float> line_lengths(const std::vector<GridTrack>& tracks, std::span<const LayoutChild> children) const {
        std::vector<float> lines;
        for (std::size_t start = 0; start < children.size(); start += tracks.size()) {
            float length = 0;
            for (std::size_t i = start; i < std::min(children.size(), start + tracks.size()); ++i) {
                length = std::max(length, along(children[i].size_that_fits(proposal(tracks[i - start].size, std::nullopt))));
            }
            lines.push_back(length);
        }
        return lines;
    }

    Size size_that_fits(ProposedSize offered, std::span<const LayoutChild> children) const {
        const std::vector<GridTrack> tracks = resolve_grid_tracks(items, finite(across(offered)), line_spacing);
        const std::vector<float> lines = line_lengths(tracks, children);
        const float gap = spacing_or_default(line_spacing);
        const float along_length = lines.empty() ? 0.f
                                                 : std::accumulate(lines.begin(), lines.end(), 0.f) +
                                                       gap * static_cast<float>(lines.size() - 1);
        return size(grid_tracks_length(tracks), along_length);
    }

    void place_children(Rect bounds, std::span<const LayoutChild> children) const {
        const float bounds_across = across(bounds.size());
        const std::vector<GridTrack> tracks = resolve_grid_tracks(items, bounds_across, line_spacing);
        const std::vector<float> lines = line_lengths(tracks, children);
        const float gap = spacing_or_default(line_spacing);
        // Tracks narrower than the grid sit at the alignment's edge across the axis.
        const float slack = std::max(0.f, bounds_across - grid_tracks_length(tracks));
        const float start = axis == Axis::horizontal ? bounds.x + slack * fraction(alignment.horizontal)
                                                     : bounds.y + slack * fraction(alignment.vertical);
        float line_position = axis == Axis::horizontal ? bounds.y : bounds.x;
        for (std::size_t line = 0; line < lines.size(); ++line) {
            float track_position = start;
            for (std::size_t t = 0; t < tracks.size(); ++t) {
                const std::size_t i = line * tracks.size() + t;
                if (i >= children.size()) {
                    break;
                }
                const Rect cell = axis == Axis::horizontal
                                      ? Rect{track_position, line_position, tracks[t].size, lines[line]}
                                      : Rect{line_position, track_position, lines[line], tracks[t].size};
                const Alignment cell_alignment =
                    children[i].value<GridCellAlignmentKey>().value_or(tracks[t].alignment.value_or(alignment));
                place_in_cell(children[i], cell, cell_alignment);
                track_position += tracks[t].size + tracks[t].spacing;
            }
            line_position += lines[line] + gap;
        }
    }
};

// --- Grid ---

struct Cell {
    std::size_t child;
    std::size_t column;
    std::size_t span;
};

struct Row {
    std::vector<Cell> cells;
    std::optional<VerticalAlignment> alignment;
    std::optional<std::size_t> full_width; // a child that isn't in a GridRow: the whole row
};

// Groups children into rows: consecutive cells of one GridRow, or single other children.
std::vector<Row> rows_of(std::span<const LayoutChild> children) {
    std::vector<Row> rows;
    std::uint64_t current = 0;
    std::uint32_t last_index = 0;
    for (std::size_t i = 0; i < children.size(); ++i) {
        const GridRowCell cell = children[i].value<GridRowKey>();
        if (cell.row == 0) {
            rows.push_back({.full_width = i});
            current = 0;
            continue;
        }
        // A new row: another GridRow, or the same one repeated (its cells start over).
        if (cell.row != current || cell.index <= last_index || rows.empty() || rows.back().full_width) {
            rows.push_back({.alignment = cell.alignment});
        }
        current = cell.row;
        last_index = cell.index;
        Row& row = rows.back();
        const std::size_t column = row.cells.empty() ? 0 : row.cells.back().column + row.cells.back().span;
        const auto span = static_cast<std::size_t>(std::max(1, children[i].value<GridCellColumnsKey>()));
        row.cells.push_back({i, column, span});
    }
    return rows;
}

struct GridMetrics {
    std::vector<Row> rows;
    std::vector<float> widths;  // of the columns
    std::vector<float> heights; // of the rows
    float column_gap = 0;
    float row_gap = 0;
    float content_width = 0;

    float span_width(const Cell& cell) const {
        float width = column_gap * static_cast<float>(cell.span - 1);
        for (std::size_t c = cell.column; c < cell.column + cell.span; ++c) {
            width += widths[c];
        }
        return width;
    }
    float height() const {
        return heights.empty() ? 0.f
                               : std::accumulate(heights.begin(), heights.end(), 0.f) +
                                     row_gap * static_cast<float>(heights.size() - 1);
    }
};

GridMetrics measure_grid(std::span<const LayoutChild> children, std::optional<float> width, float column_gap,
                         float row_gap) {
    GridMetrics m;
    m.rows = rows_of(children);
    m.column_gap = column_gap;
    m.row_gap = row_gap;
    std::size_t columns = 0;
    for (const Row& row : m.rows) {
        if (!row.cells.empty()) {
            columns = std::max(columns, row.cells.back().column + row.cells.back().span);
        }
    }

    // Ideal widths: each column fits its widest single-column cell; wider spanning cells widen
    // the columns they span evenly.
    m.widths.assign(columns, 0.f);
    std::vector<float> maxima(columns, 0.f); // how wide each column's cells would go
    std::vector<const Cell*> spanning;
    for (const Row& row : m.rows) {
        for (const Cell& cell : row.cells) {
            if (cell.span > 1) {
                spanning.push_back(&cell);
                continue;
            }
            const float ideal = children[cell.child].size_that_fits(ProposedSize::unspecified()).width;
            const float widest = children[cell.child].size_that_fits({infinity, std::nullopt}).width;
            m.widths[cell.column] = std::max(m.widths[cell.column], ideal);
            maxima[cell.column] = std::max(maxima[cell.column], widest);
        }
    }
    std::ranges::stable_sort(spanning, {}, [](const Cell* cell) { return cell->span; });
    for (const Cell* cell : spanning) {
        const float ideal = children[cell->child].size_that_fits(ProposedSize::unspecified()).width;
        const float have = m.span_width(*cell);
        if (ideal > have) {
            const float extra = (ideal - have) / static_cast<float>(cell->span);
            for (std::size_t c = cell->column; c < cell->column + cell->span; ++c) {
                m.widths[c] += extra;
                maxima[c] = std::max(maxima[c], m.widths[c]);
            }
        }
    }

    const float gaps = columns > 0 ? column_gap * static_cast<float>(columns - 1) : 0.f;
    const float ideal_width = std::accumulate(m.widths.begin(), m.widths.end(), 0.f) + gaps;
    if (width && columns > 0) {
        if (*width > ideal_width) {
            // Columns with flexible cells share the extra width, each up to how wide they'd go.
            float extra = *width - ideal_width;
            std::vector<std::size_t> open;
            for (std::size_t c = 0; c < columns; ++c) {
                if (maxima[c] > m.widths[c]) {
                    open.push_back(c);
                }
            }
            while (extra > 0.01f && !open.empty()) {
                const float each = extra / static_cast<float>(open.size());
                std::vector<std::size_t> still_open;
                for (const std::size_t c : open) {
                    const float add = std::min(each, maxima[c] - m.widths[c]);
                    m.widths[c] += add;
                    extra -= add;
                    if (maxima[c] > m.widths[c]) {
                        still_open.push_back(c);
                    }
                }
                if (still_open.size() == open.size()) {
                    break; // everyone took a full share
                }
                open = std::move(still_open);
            }
        } else if (*width < ideal_width) {
            // Too narrow: every column gives up width in proportion (text wraps).
            const float total = ideal_width - gaps;
            const float scale = total > 0 ? std::max(0.f, *width - gaps) / total : 0.f;
            for (float& w : m.widths) {
                w *= scale;
            }
        }
    }
    m.content_width = std::accumulate(m.widths.begin(), m.widths.end(), 0.f) + gaps;

    // Rows that are a single child take the grid's width; with no columns, the widest of them.
    if (columns == 0) {
        for (const Row& row : m.rows) {
            const float ideal = children[*row.full_width].size_that_fits({width, std::nullopt}).width;
            m.content_width = std::max(m.content_width, ideal);
        }
    }

    m.heights.reserve(m.rows.size());
    for (const Row& row : m.rows) {
        float height = 0;
        if (row.full_width) {
            height = children[*row.full_width].size_that_fits({m.content_width, std::nullopt}).height;
        }
        for (const Cell& cell : row.cells) {
            height = std::max(height, children[cell.child].size_that_fits({m.span_width(cell), std::nullopt}).height);
        }
        m.heights.push_back(height);
    }
    return m;
}

} // namespace

// --- Tracks ---

std::vector<GridTrack> resolve_grid_tracks(std::span<const GridItem> given, std::optional<float> length,
                                           OptionalLength spacing) {
    std::vector<GridItem> items(given.begin(), given.end());
    if (items.empty()) {
        items.push_back(GridItem::flexible());
    }
    const float default_gap = spacing.value_or(default_stack_spacing);
    const auto gap = [&](const GridItem& item) { return item.spacing.value_or(default_gap); };
    const std::optional<float> available = finite(length);

    // Fixed items take their size; the rest share what's left (after the gaps between items).
    std::vector<float> share(items.size(), 0.f);
    std::vector<std::size_t> open;
    float remaining = available.value_or(0.f);
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (i + 1 < items.size()) {
            remaining -= gap(items[i]);
        }
        if (items[i].kind == GridItem::Kind::fixed) {
            share[i] = std::max(0.f, items[i].minimum);
            remaining -= share[i];
        } else {
            open.push_back(i);
        }
    }
    if (!available) {
        // Ideal sizes: flexible items at their minimum, adaptive ones as one track.
        for (const std::size_t i : open) {
            share[i] = std::max(0.f, items[i].minimum);
        }
    } else {
        // Equal shares; an item whose limit binds takes its limit, and the others split the rest
        // again. Adaptive items have no limits here: their minimum decides how many tracks fit
        // (one, squeezed, if even one doesn't), and their maximum caps each track.
        remaining = std::max(0.f, remaining);
        while (!open.empty()) {
            const float each = remaining / static_cast<float>(open.size());
            std::vector<std::size_t> unbound;
            bool bound = false;
            for (const std::size_t i : open) {
                const bool adaptive = items[i].kind == GridItem::Kind::adaptive;
                const float low = adaptive ? 0.f : std::max(0.f, items[i].minimum);
                const float high = adaptive ? infinity : std::max(low, items[i].maximum);
                const float clamped = std::clamp(each, low, high);
                if (clamped != each) {
                    share[i] = clamped;
                    remaining -= clamped;
                    bound = true;
                } else {
                    unbound.push_back(i);
                }
            }
            if (!bound) {
                for (const std::size_t i : unbound) {
                    share[i] = each;
                }
                break;
            }
            remaining = std::max(0.f, remaining);
            open = std::move(unbound);
        }
    }

    std::vector<GridTrack> tracks;
    for (std::size_t i = 0; i < items.size(); ++i) {
        const GridItem& item = items[i];
        if (item.kind != GridItem::Kind::adaptive) {
            tracks.push_back({share[i], gap(item), item.alignment});
            continue;
        }
        const float g = gap(item);
        const float minimum = std::max(1.f, item.minimum);
        std::size_t count = 1;
        float size = minimum;
        if (available) {
            count = static_cast<std::size_t>(std::max(1.f, std::floor((share[i] + g) / (minimum + g))));
            size = std::min(std::max(0.f, item.maximum),
                            (share[i] - g * static_cast<float>(count - 1)) / static_cast<float>(count));
        }
        for (std::size_t n = 0; n < count; ++n) {
            tracks.push_back({std::max(0.f, size), g, item.alignment});
        }
    }
    return tracks;
}

float grid_tracks_length(std::span<const GridTrack> tracks) noexcept {
    float length = 0;
    for (std::size_t i = 0; i < tracks.size(); ++i) {
        length += tracks[i].size + (i + 1 < tracks.size() ? tracks[i].spacing : 0.f);
    }
    return length;
}

// --- Layouts ---

Size VGridLayout::size_that_fits(ProposedSize proposal, std::span<const LayoutChild> children) const {
    return LineGrid{Axis::horizontal, columns_, alignment_, spacing_}.size_that_fits(proposal, children);
}

void VGridLayout::place_children(Rect bounds, ProposedSize, std::span<const LayoutChild> children) const {
    LineGrid{Axis::horizontal, columns_, alignment_, spacing_}.place_children(bounds, children);
}

Size HGridLayout::size_that_fits(ProposedSize proposal, std::span<const LayoutChild> children) const {
    return LineGrid{Axis::vertical, rows_, alignment_, spacing_}.size_that_fits(proposal, children);
}

void HGridLayout::place_children(Rect bounds, ProposedSize, std::span<const LayoutChild> children) const {
    LineGrid{Axis::vertical, rows_, alignment_, spacing_}.place_children(bounds, children);
}

Size GridLayout::size_that_fits(ProposedSize proposal, std::span<const LayoutChild> children) const {
    const GridMetrics m = measure_grid(children, finite(proposal.width), spacing_or_default(horizontal_spacing_),
                                       spacing_or_default(vertical_spacing_));
    return {m.content_width, m.height()};
}

void GridLayout::place_children(Rect bounds, ProposedSize, std::span<const LayoutChild> children) const {
    const GridMetrics m = measure_grid(children, bounds.width, spacing_or_default(horizontal_spacing_),
                                       spacing_or_default(vertical_spacing_));
    // A column's alignment: the first of its cells that sets one.
    std::vector<std::optional<HorizontalAlignment>> column_alignment(m.widths.size());
    for (const Row& row : m.rows) {
        for (const Cell& cell : row.cells) {
            if (!column_alignment[cell.column]) {
                column_alignment[cell.column] = children[cell.child].value<GridColumnAlignmentKey>();
            }
        }
    }
    std::vector<float> column_x(m.widths.size());
    const float left = bounds.x + std::max(0.f, bounds.width - m.content_width) * fraction(alignment_.horizontal);
    float x = left;
    for (std::size_t c = 0; c < m.widths.size(); ++c) {
        column_x[c] = x;
        x += m.widths[c] + m.column_gap;
    }

    float y = bounds.y;
    for (std::size_t r = 0; r < m.rows.size(); ++r) {
        const Row& row = m.rows[r];
        const float height = m.heights[r];
        if (row.full_width) {
            const LayoutChild& child = children[*row.full_width];
            place_in_cell(child, {left, y, m.content_width, height},
                          child.value<GridCellAlignmentKey>().value_or(alignment_));
        }
        for (const Cell& cell : row.cells) {
            const LayoutChild& child = children[cell.child];
            const Alignment fallback{column_alignment[cell.column].value_or(alignment_.horizontal),
                                     row.alignment.value_or(alignment_.vertical)};
            place_in_cell(child, {column_x[cell.column], y, m.span_width(cell), height},
                          child.value<GridCellAlignmentKey>().value_or(fallback));
        }
        y += height + m.row_gap;
    }
}

// --- Rows and cell modifiers ---

View detail::make_grid_row(GridRowOptions options, std::vector<View> cells) {
    static std::atomic<std::uint64_t> next_row{1};
    const std::uint64_t row = next_row++;
    auto items = std::make_shared<std::vector<ForEachItem>>();
    items->reserve(cells.size());
    const auto value = std::make_shared<const int>(0);
    for (std::size_t i = 0; i < cells.size(); ++i) {
        View cell = cells[i].layout_value<GridRowKey>(GridRowCell{row, static_cast<std::uint32_t>(i), options.alignment});
        items->push_back({ItemKey(static_cast<std::int64_t>(i)), value,
                          [](const void*, const void*) { return true; }, [cell] { return cell; }});
    }
    // A fragment: inside a Grid, the cells are the grid's children.
    return make_for_each([items] { return *items; });
}

View View::grid_cell_columns(int count) const {
    return layout_value<GridCellColumnsKey>(std::max(1, count));
}

View View::grid_cell_alignment(Alignment alignment) const {
    return layout_value<GridCellAlignmentKey>(alignment);
}

View View::grid_column_alignment(HorizontalAlignment alignment) const {
    return layout_value<GridColumnAlignmentKey>(alignment);
}

} // namespace elg
