// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_VIEW_INSPECTOR_H
#define ELG_VIEW_INSPECTOR_H

#include "render/display_list.h"
#include "view/element.h"

#include <elg/reactive.h>
#include <elg/view.h>
#include <elg/views.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace elg::detail {

// Where an element is in a tree: child indexes from the root (for_each_child order). Paths, not
// pointers, so the inspector never holds on to elements that may be gone.
using ElementPath = std::vector<int>;

// One element of an inspected tree, flattened depth first.
struct InspectRow {
    ElementPath path;
    int depth = 0;
    int children = 0;
    Inspection info;
    Rect frame;
    std::optional<ProposedSize> proposal;

    bool operator==(const InspectRow& other) const {
        return path == other.path && children == other.children && info.name == other.info.name &&
               info.detail == other.info.detail && info.properties == other.info.properties &&
               frame == other.frame && proposal == other.proposal;
    }
};

// What the inspected window's last frame cost.
struct FrameStats {
    double build_ms = 0; // layout + paint on the UI thread
    std::size_t elements = 0;
    std::size_t commands = 0;
    std::size_t glyphs = 0;
    std::size_t paths = 0;
    std::size_t layers = 0; // offscreen passes
    bool operator==(const FrameStats&) const = default;
};

std::vector<InspectRow> inspect_tree(Element& root);
Element* element_at(Element& root, const ElementPath& path);
std::optional<ElementPath> path_of(Element& root, const Element& target);
FrameStats count_frame(const DisplayList& list, double build_ms, std::size_t elements);

// The inspector's state (design.md §16): one inspected window at a time. The App drives it: it
// refreshes the rows, reports frame stats, and routes picking; the Inspector window's view reads
// and writes the states.
struct Inspector {
    std::uint32_t target = 0; // the inspected window
    State<std::string> title{std::string()};
    State<std::vector<InspectRow>> rows{std::vector<InspectRow>()};
    State<std::optional<ElementPath>> selected{std::optional<ElementPath>()};
    State<bool> picking{false};
    State<FrameStats> stats{FrameStats{}};
    State<bool> refresh_requested{false};
    State<std::vector<ElementPath>> collapsed{std::vector<ElementPath>()};
    ScrollPosition tree_position;       // the tree's scroll view
    std::optional<ElementPath> hovered; // while picking (drawn in the target window only)
    double last_stats = 0;              // when stats were last published
};

// Selects the element at `path`: expands the rows around it and scrolls the tree to it.
void reveal_in_inspector(Inspector& inspector, const ElementPath& path);

// The Inspector window's content.
View InspectorView(std::shared_ptr<Inspector> inspector);

// Draws the selection and pick highlights over the inspected window's content.
void paint_inspector_overlay(const Inspector& inspector, Element& root, DisplayList& list);

} // namespace elg::detail

#endif // ELG_VIEW_INSPECTOR_H
