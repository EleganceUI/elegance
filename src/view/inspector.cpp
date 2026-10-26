// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include "view/inspector.h"

#include <elg/image.h>
#include <elg/layout.h>
#include <elg/shapes.h>
#include <elg/styles.h>
#include <elg/text.h>
#include <elg/theme.h>
#include <elg/views.h>

#include <format>

namespace elg::detail {

namespace {

void collect(Element& element, ElementPath& path, std::vector<InspectRow>& rows) {
    InspectRow row;
    row.path = path;
    row.depth = static_cast<int>(path.size());
    element.inspect(row.info);
    row.frame = element.frame();
    row.proposal = element.last_proposal();
    const std::size_t index = rows.size();
    rows.push_back(std::move(row));
    int child = 0;
    element.for_each_child([&](Element& c) {
        path.push_back(child++);
        collect(c, path, rows);
        path.pop_back();
    });
    rows[index].children = child;
}

std::string format_length(std::optional<float> v) {
    if (!v) {
        return "nil";
    }
    if (std::isinf(*v)) {
        return "∞";
    }
    return *v == std::floor(*v) ? std::format("{}", static_cast<int>(*v)) : std::format("{:.1f}", *v);
}

std::string path_key(const ElementPath& path) {
    std::string key;
    for (const int i : path) {
        key += std::to_string(i);
        key += '.';
    }
    return key;
}

} // namespace

std::vector<InspectRow> inspect_tree(Element& root) {
    std::vector<InspectRow> rows;
    ElementPath path;
    collect(root, path, rows);
    return rows;
}

Element* element_at(Element& root, const ElementPath& path) {
    Element* current = &root;
    for (const int index : path) {
        Element* found = nullptr;
        int i = 0;
        current->for_each_child([&](Element& child) {
            if (i++ == index) {
                found = &child;
            }
        });
        if (!found) {
            return nullptr;
        }
        current = found;
    }
    return current;
}

std::optional<ElementPath> path_of(Element& root, const Element& target) {
    ElementPath reversed;
    const Element* current = &target;
    while (current != &root) {
        Element* parent = current->parent();
        if (!parent) {
            return std::nullopt; // not in this tree
        }
        int index = -1;
        int i = 0;
        parent->for_each_child([&](Element& child) {
            if (&child == current) {
                index = i;
            }
            ++i;
        });
        if (index < 0) {
            return std::nullopt; // e.g. a removed child still animating out
        }
        reversed.push_back(index);
        current = parent;
    }
    return ElementPath(reversed.rbegin(), reversed.rend());
}

FrameStats count_frame(const DisplayList& list, double build_ms, std::size_t elements) {
    FrameStats stats;
    stats.build_ms = build_ms;
    stats.elements = elements;
    stats.commands = list.commands.size();
    for (const DrawCommand& command : list.commands) {
        if (std::holds_alternative<GlyphCommand>(command)) {
            ++stats.glyphs;
        } else if (std::holds_alternative<PathCommand>(command)) {
            ++stats.paths;
        } else if (const auto* layer = std::get_if<PushLayerCommand>(&command)) {
            ++stats.layers;
            stats.paths += layer->mask ? 1u : 0u;
        }
    }
    return stats;
}

void paint_inspector_overlay(const Inspector& inspector, Element& root, DisplayList& list) {
    const auto highlight = [&](const std::optional<ElementPath>& path, Color color) {
        if (!path) {
            return;
        }
        if (const Element* element = element_at(root, *path)) {
            list.commands.push_back(RectCommand{element->frame(), {}, ShapeStyle(color.opacity(0.12f)), color, 1.5f});
        }
    };
    constexpr Color kSelected{0.24f, 0.56f, 1.f, 1};
    constexpr Color kHovered{1.f, 0.55f, 0.2f, 1};
    highlight(inspector.selected.peek(), kSelected);
    if (inspector.picking.peek()) {
        highlight(inspector.hovered, kHovered);
    }
}

void reveal_in_inspector(Inspector& inspector, const ElementPath& path) {
    inspector.collapsed.update([&](std::vector<ElementPath>& collapsed) {
        std::erase_if(collapsed, [&](const ElementPath& c) {
            return c.size() < path.size() && std::equal(c.begin(), c.end(), path.begin());
        });
    });
    inspector.selected = path;
    inspector.tree_position.scroll_to(path_key(path));
}

// --- The Inspector window ---

namespace {

bool is_collapsed(const Inspector& inspector, const ElementPath& path) {
    return std::ranges::find(inspector.collapsed(), path) != inspector.collapsed().end();
}

// The rows not inside a collapsed row.
std::vector<InspectRow> visible_rows(const Inspector& inspector) {
    std::vector<InspectRow> out;
    int hidden_below = -1; // rows deeper than this are inside a collapsed row
    for (const InspectRow& row : inspector.rows()) {
        if (hidden_below >= 0 && row.depth > hidden_below) {
            continue;
        }
        hidden_below = row.children > 0 && is_collapsed(inspector, row.path) ? row.depth : -1;
        out.push_back(row);
    }
    return out;
}

View TreeRow(const InspectRow& row, std::shared_ptr<Inspector> inspector) {
    const ElementPath path = row.path;
    // A chevron to collapse or expand rows with children.
    const View disclosure =
        row.children == 0
            ? View(Spacer().frame(12, 12))
            : View(ZStack(Image("caret-right").opacity([inspector, path] { return is_collapsed(*inspector, path) ? 1.f : 0.f; }),
                          Image("caret-down").opacity([inspector, path] { return is_collapsed(*inspector, path) ? 0.f : 1.f; }))
                       .foreground_style(ThemeColor::tertiary)
                       .frame(12, 12)
                       .content_shape(Rectangle())
                       .on_tap_gesture([inspector, path] {
                           inspector->collapsed.update([&](std::vector<ElementPath>& collapsed) {
                               if (const auto it = std::ranges::find(collapsed, path); it != collapsed.end()) {
                                   collapsed.erase(it);
                               } else {
                                   collapsed.push_back(path);
                               }
                           });
                       }));
    const std::string size = std::format("{} × {}", format_length(row.frame.width), format_length(row.frame.height));
    View label = HStack({.spacing = 6},
                        disclosure,
                        Text(row.info.name).font_weight(Weight::semibold),
                        Text(row.info.detail).foreground_style(ThemeColor::secondary).line_limit(1),
                        Spacer(),
                        Text(size).foreground_style(ThemeColor::tertiary));
    return label.padding(EdgeInsets{.leading = 12.f * static_cast<float>(row.depth)})
        .padding(EdgeInsets{.top = 3, .leading = 6, .bottom = 3, .trailing = 6})
        .frame(FlexibleFrameOptions{.max_width = infinity, .alignment = Alignment::leading})
        .background(RoundedRectangle(4).fill(ThemeColor::accent).opacity([inspector, path] {
            const std::optional<ElementPath>& selected = inspector->selected();
            return selected && *selected == path ? 0.25f : 0.f;
        }))
        .content_shape(Rectangle())
        .on_tap_gesture([inspector, path] { inspector->selected = path; });
}

// The selected row, if it's still in the tree.
std::optional<InspectRow> selected_row(const Inspector& inspector) {
    const std::optional<ElementPath>& selected = inspector.selected();
    if (!selected) {
        return std::nullopt;
    }
    for (const InspectRow& row : inspector.rows()) {
        if (row.path == *selected) {
            return row;
        }
    }
    return std::nullopt;
}

using Property = std::pair<std::string, std::string>;

std::vector<Property> details(const Inspector& inspector) {
    const std::optional<InspectRow> row = selected_row(inspector);
    if (!row) {
        return {{"", "Select a view in the tree, or Pick one in the window."}};
    }
    std::vector<Property> out;
    out.emplace_back("view", row->info.detail.empty() ? row->info.name : row->info.name + "  " + row->info.detail);
    out.emplace_back("frame", std::format("x {}, y {}, {} × {}", format_length(row->frame.x), format_length(row->frame.y),
                                          format_length(row->frame.width), format_length(row->frame.height)));
    if (row->proposal) {
        out.emplace_back("proposed", std::format("{} × {}", format_length(row->proposal->width),
                                                 format_length(row->proposal->height)));
    }
    out.emplace_back("children", std::to_string(row->children));
    for (const Property& property : row->info.properties) {
        out.push_back(property);
    }
    return out;
}

} // namespace

View InspectorView(std::shared_ptr<Inspector> inspector) {
    const View toolbar = HStack(
        {.spacing = 8},
        Text([inspector] { return inspector->title(); }).font_weight(Weight::semibold).line_limit(1),
        Spacer(),
        Button(HStack({.spacing = 4}, Image("crosshair"), Text([inspector] {
                          return std::string(inspector->picking() ? "Picking…" : "Pick");
                      })),
               [inspector] { inspector->picking = !inspector->picking.peek(); })
            .button_style(BorderedButtonStyle{}),
        Button(HStack({.spacing = 4}, Image("arrow-clockwise"), Text("Refresh")),
               [inspector] { inspector->refresh_requested = true; })
            .button_style(BorderedButtonStyle{}));

    const View stats = HStack(
        {.spacing = 10},
        Text([inspector] {
            const FrameStats& s = inspector->stats();
            return std::format("{:.2f} ms · {} views · {} cmds · {} glyphs · {} paths", s.build_ms, s.elements,
                               s.commands, s.glyphs, s.paths);
        }).foreground_style(ThemeColor::secondary),
        Spacer(),
        Text([inspector] {
            const std::size_t layers = inspector->stats().layers;
            return layers == 0 ? std::string() : std::format("{} offscreen pass{}", layers, layers == 1 ? "" : "es");
        }).foreground_style(ThemeColor::warning));

    const View tree = ScrollView({.position = inspector->tree_position}, LazyVStack({.alignment = HorizontalAlignment::leading, .spacing = 0},
                                            ForEach([inspector] { return visible_rows(*inspector); },
                                                    [](const InspectRow& row) { return path_key(row.path); },
                                                    [inspector](const InspectRow& row) { return TreeRow(row, inspector); })))
                          .padding(4)
                          .background(RoundedRectangle(8).fill(ThemeColor::surface));

    const View detail_rows =
        VStack({.alignment = HorizontalAlignment::leading, .spacing = 3},
               ForEach([inspector] { return details(*inspector); },
                       [](const Property& p) { return p.first + "\n" + p.second; },
                       [](const Property& p) -> View {
                           if (p.first.empty()) {
                               return Text(p.second).foreground_style(ThemeColor::secondary);
                           }
                           return HStack({.alignment = VerticalAlignment::top, .spacing = 8},
                                         Text(p.first).foreground_style(ThemeColor::secondary).frame({.width = 70, .alignment = Alignment::leading}),
                                         Text(p.second).line_limit(2), Spacer());
                       }));

    return VStack({.alignment = HorizontalAlignment::leading, .spacing = 8},
                  toolbar, stats, tree.frame(FlexibleFrameOptions{.max_width = infinity, .max_height = infinity}),
                  detail_rows.padding(10)
                      .frame(FlexibleFrameOptions{.max_width = infinity, .alignment = Alignment::top_leading})
                      .background(RoundedRectangle(8).fill(ThemeColor::surface)))
        .padding(12)
        .font(Font::system(12))
        .frame(FlexibleFrameOptions{.max_width = infinity, .max_height = infinity, .alignment = Alignment::top_leading});
}

} // namespace elg::detail
