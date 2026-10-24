// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include "model.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <format>
#include <functional>
#include <limits>
#include <unordered_map>

namespace design {

using namespace elg;

namespace {

constexpr std::size_t kHistoryLimit = 200;

bool inside(Rect r, Point p) {
    return p.x >= r.x && p.y >= r.y && p.x <= r.max_x() && p.y <= r.max_y();
}

Rect union_of(Rect a, Rect b) {
    const float x = std::min(a.x, b.x);
    const float y = std::min(a.y, b.y);
    return {x, y, std::max(a.max_x(), b.max_x()) - x, std::max(a.max_y(), b.max_y()) - y};
}

const char* kind_name(Kind kind) {
    switch (kind) {
    case Kind::frame: return "Frame";
    case Kind::group: return "Group";
    case Kind::rectangle: return "Rectangle";
    case Kind::ellipse: return "Ellipse";
    case Kind::line: return "Line";
    case Kind::text: return "Text";
    }
    return "Layer";
}

std::unordered_map<int, std::size_t> index_of(const std::vector<Node>& nodes) {
    std::unordered_map<int, std::size_t> index;
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        index[nodes[i].id] = i;
    }
    return index;
}

// `id` and everything inside it.
void collect_subtree(const Children& children, int id, std::vector<int>& out) {
    out.push_back(id);
    if (const auto it = children.find(id); it != children.end()) {
        for (const int child : it->second) {
            collect_subtree(children, child, out);
        }
    }
}

} // namespace

Rect Node::bounds() const {
    if (kind == Kind::line) {
        return {std::min(x, x + w), std::min(y, y + h), std::abs(w), std::abs(h)};
    }
    return {x, y, w, h};
}

Size estimate_text_size(const std::string& text, float font_size, bool bold) {
    // Average glyph advance of a UI font, a little wider for bold; lines at 1.3 em.
    float widest = 0;
    float line = 0;
    int lines = 1;
    for (const char c : text) {
        if (c == '\n') {
            widest = std::max(widest, line);
            line = 0;
            ++lines;
        } else if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) { // count UTF-8 characters
            line += font_size * (bold ? 0.58f : 0.54f);
        }
    }
    widest = std::max(widest, line);
    return {std::max(widest, font_size * 0.6f), font_size * 1.3f * static_cast<float>(lines)};
}

// --- The tree ---

Children children_of(const std::vector<Node>& nodes) {
    std::unordered_map<int, bool> exists;
    for (const Node& n : nodes) {
        exists[n.id] = true;
    }
    Children children;
    children[0];
    for (const Node& n : nodes) {
        const int parent = n.parent != 0 && exists.contains(n.parent) && n.parent != n.id ? n.parent : 0;
        children[parent].push_back(n.id);
    }
    return children;
}

std::vector<Node> flatten(const std::vector<Node>& nodes, const Children& children) {
    const std::unordered_map<int, std::size_t> index = index_of(nodes);
    std::vector<Node> result;
    result.reserve(nodes.size());
    std::unordered_map<int, bool> placed;
    const std::function<void(int, int)> visit = [&](int id, int parent) {
        if (placed[id]) {
            return; // a cycle; drop it
        }
        placed[id] = true;
        Node n = nodes[index.at(id)];
        n.parent = parent;
        result.push_back(n);
        if (const auto it = children.find(id); it != children.end()) {
            for (const int child : it->second) {
                if (index.contains(child)) {
                    visit(child, id);
                }
            }
        }
    };
    for (const int id : children.at(0)) {
        if (index.contains(id)) {
            visit(id, 0);
        }
    }
    return result;
}

std::vector<Node> normalize(std::vector<Node> nodes, const std::set<int>& floating) {
    nodes = flatten(nodes, children_of(nodes));
    Children children = children_of(nodes);
    std::unordered_map<int, std::size_t> index = index_of(nodes);
    const auto shift_subtree = [&](int id, float dx, float dy) {
        if (dx == 0 && dy == 0) {
            return;
        }
        std::vector<int> ids;
        collect_subtree(children, id, ids);
        for (const int i : ids) {
            nodes[index[i]].x += dx;
            nodes[index[i]].y += dy;
        }
    };
    // Children before parents: the list is in tree order, so backwards.
    std::vector<int> empty_groups;
    for (std::size_t i = nodes.size(); i-- > 0;) {
        const int id = nodes[i].id;
        const std::vector<int>& kids = children[id];
        if (nodes[i].kind == Kind::group) {
            std::optional<Rect> area;
            for (const int k : kids) {
                const Rect b = nodes[index[k]].bounds();
                area = area ? union_of(*area, b) : b;
            }
            if (!area) {
                empty_groups.push_back(id);
                continue;
            }
            nodes[i].x = area->x;
            nodes[i].y = area->y;
            nodes[i].w = area->width;
            nodes[i].h = area->height;
            continue;
        }
        if (nodes[i].kind != Kind::frame || !nodes[i].auto_layout) {
            continue;
        }
        Node& frame = nodes[i];
        std::vector<int> laid_out;
        for (const int k : kids) {
            if (nodes[index[k]].visible && !floating.contains(k)) {
                laid_out.push_back(k);
            }
        }
        const bool row = frame.horizontal;
        const auto main = [&](Rect r) { return row ? r.width : r.height; };
        const auto cross = [&](Rect r) { return row ? r.height : r.width; };
        float total = 0;
        float widest = 0;
        for (const int k : laid_out) {
            const Rect b = nodes[index[k]].bounds();
            total += main(b);
            widest = std::max(widest, cross(b));
        }
        if (!laid_out.empty()) {
            total += frame.gap * static_cast<float>(laid_out.size() - 1);
        }
        if (frame.hug) {
            (row ? frame.w : frame.h) = std::max(total + 2 * frame.padding, 1.f);
            if (frame.align != CrossAlign::stretch) {
                (row ? frame.h : frame.w) = std::max(widest + 2 * frame.padding, 1.f);
            }
        }
        const float cross_space = (row ? frame.h : frame.w) - 2 * frame.padding;
        float cursor = (row ? frame.x : frame.y) + frame.padding;
        const float cross_start = (row ? frame.y : frame.x) + frame.padding;
        for (const int k : laid_out) {
            Node& child = nodes[index[k]];
            if (frame.align == CrossAlign::stretch && child.kind != Kind::line) {
                (row ? child.h : child.w) = std::max(cross_space, 1.f);
            }
            const Rect b = child.bounds();
            float offset = 0;
            switch (frame.align) {
            case CrossAlign::start:
            case CrossAlign::stretch: offset = 0; break;
            case CrossAlign::center: offset = (cross_space - cross(b)) / 2; break;
            case CrossAlign::end: offset = cross_space - cross(b); break;
            }
            const float target_main = cursor;
            const float target_cross = cross_start + offset;
            const float dx = (row ? target_main : target_cross) - b.x;
            const float dy = (row ? target_cross : target_main) - b.y;
            shift_subtree(k, dx, dy);
            cursor += main(b) + frame.gap;
        }
    }
    if (!empty_groups.empty()) {
        std::erase_if(nodes, [&](const Node& n) { return std::ranges::contains(empty_groups, n.id); });
        // Groups that held only the removed ones are empty now: fit again.
        return normalize(std::move(nodes), floating);
    }
    return nodes;
}

// --- Document ---

void Document::set_nodes(std::vector<Node> next) const {
    nodes = normalize(std::move(next), *floating);
}

void Document::edit(const std::function<void(std::vector<Node>&)>& change) const {
    std::vector<Node> next = nodes.peek();
    change(next);
    set_nodes(std::move(next));
}

void Document::relayout() const {
    set_nodes(nodes.peek());
}

std::optional<Node> Document::node(int id) const {
    for (const Node& n : nodes()) {
        if (n.id == id) {
            return n;
        }
    }
    return std::nullopt;
}

bool Document::is_selected(int id) const {
    return std::ranges::contains(selection(), id);
}

std::vector<Node> Document::selected() const {
    std::vector<Node> result;
    const std::vector<int>& ids = selection();
    for (const Node& n : nodes()) {
        if (std::ranges::contains(ids, n.id)) {
            result.push_back(n);
        }
    }
    return result;
}

std::optional<Rect> Document::selection_bounds() const {
    std::optional<Rect> result;
    for (const Node& n : selected()) {
        result = result ? union_of(*result, n.bounds()) : n.bounds();
    }
    return result;
}

std::vector<int> Document::descendants(int id) const {
    std::vector<int> ids;
    collect_subtree(children_of(nodes.peek()), id, ids);
    ids.erase(ids.begin());
    return ids;
}

bool Document::is_ancestor(int ancestor, int id) const {
    const std::vector<Node>& all = nodes.peek();
    const std::unordered_map<int, std::size_t> index = index_of(all);
    int current = id;
    for (int guard = 0; current != 0 && guard < 1000; ++guard) {
        const auto it = index.find(current);
        if (it == index.end()) {
            return false;
        }
        current = all[it->second].parent;
        if (current == ancestor) {
            return true;
        }
    }
    return false;
}

int Document::depth(int id) const {
    const std::vector<Node>& all = nodes.peek();
    const std::unordered_map<int, std::size_t> index = index_of(all);
    int d = 0;
    for (int current = id; current != 0 && d < 1000; ++d) {
        const auto it = index.find(current);
        if (it == index.end()) {
            break;
        }
        current = all[it->second].parent;
    }
    return std::max(d - 1, 0);
}

int Document::hit(Point p) const {
    const std::vector<Node>& all = nodes.peek();
    const std::unordered_map<int, std::size_t> index = index_of(all);
    const float slop = 4 / zoom.peek(); // lines and thin shapes are easier to hit
    // Hidden or locked ancestors hide and lock what's inside; clipping frames hide what's
    // outside them.
    const auto reachable = [&](const Node& n) {
        for (int current = n.parent; current != 0;) {
            const Node& a = all[index.at(current)];
            if (!a.visible || a.locked) {
                return false;
            }
            if (a.kind == Kind::frame && a.clips && !inside(a.bounds(), p)) {
                return false;
            }
            current = a.parent;
        }
        return true;
    };
    for (auto it = all.rbegin(); it != all.rend(); ++it) {
        const Node& n = *it;
        if (!n.visible || n.locked || n.kind == Kind::group || !reachable(n)) {
            continue;
        }
        const Rect b = n.bounds();
        if (n.kind == Kind::line) {
            const float dx = n.w;
            const float dy = n.h;
            const float length2 = dx * dx + dy * dy;
            float t = length2 > 0 ? ((p.x - n.x) * dx + (p.y - n.y) * dy) / length2 : 0;
            t = std::clamp(t, 0.f, 1.f);
            const float cx = n.x + t * dx - p.x;
            const float cy = n.y + t * dy - p.y;
            if (cx * cx + cy * cy <= (slop + n.stroke_width) * (slop + n.stroke_width)) {
                return n.id;
            }
            continue;
        }
        if (n.kind == Kind::ellipse) {
            const float rx = b.width / 2 + slop;
            const float ry = b.height / 2 + slop;
            const float ex = (p.x - (b.x + b.width / 2)) / std::max(rx, 0.001f);
            const float ey = (p.y - (b.y + b.height / 2)) / std::max(ry, 0.001f);
            if (ex * ex + ey * ey <= 1) {
                return n.id;
            }
            continue;
        }
        if (n.kind == Kind::frame && n.parent == 0) {
            // Top-level frames are also picked by their title above them.
            const Rect title{b.x, b.y - 22 / zoom.peek(), b.width, 22 / zoom.peek()};
            if (inside(title, p)) {
                return n.id;
            }
        }
        if (inside(Rect{b.x - slop, b.y - slop, b.width + 2 * slop, b.height + 2 * slop}, p)) {
            return n.id;
        }
    }
    return 0;
}

int Document::click_target(int id) const {
    if (id == 0) {
        return 0;
    }
    const std::vector<Node>& all = nodes.peek();
    const std::unordered_map<int, std::size_t> index = index_of(all);
    // The groups around it, outermost first.
    std::vector<int> groups;
    for (int current = all[index.at(id)].parent; current != 0; current = all[index.at(current)].parent) {
        if (all[index.at(current)].kind == Kind::group) {
            groups.insert(groups.begin(), current);
        }
    }
    const std::vector<int>& chosen = selection.peek();
    for (const int g : groups) {
        // Entered: something inside this group is selected already, so go deeper.
        const bool entered = std::ranges::any_of(chosen, [&](int s) { return s != g && is_ancestor(g, s); });
        if (!entered) {
            return g;
        }
    }
    return id;
}

int Document::frame_at(Point p, const std::vector<int>& moving) const {
    const std::vector<Node>& all = nodes.peek();
    for (auto it = all.rbegin(); it != all.rend(); ++it) {
        const Node& n = *it;
        if (n.kind != Kind::frame || !n.visible || n.locked || !inside(n.bounds(), p)) {
            continue;
        }
        if (std::ranges::any_of(moving, [&](int m) { return m == n.id || is_ancestor(m, n.id); })) {
            continue;
        }
        return n.id;
    }
    return 0;
}

Point Document::to_canvas(Point screen) const {
    const Point p = pan.peek();
    const float z = zoom.peek();
    return {(screen.x - p.x) / z, (screen.y - p.y) / z};
}

Point Document::to_screen(Point canvas) const {
    const Point p = pan.peek();
    const float z = zoom.peek();
    return {canvas.x * z + p.x, canvas.y * z + p.y};
}

// --- History ---

void Document::checkpoint() const {
    history->undo.push_back(nodes.peek());
    if (history->undo.size() > kHistoryLimit) {
        history->undo.erase(history->undo.begin());
    }
    history->redo.clear();
}

namespace {

void drop_missing(const Document& doc) {
    doc.selection.update([&](std::vector<int>& ids) {
        std::erase_if(ids, [&](int id) { return !untrack([&] { return doc.node(id).has_value(); }); });
    });
}

} // namespace

void Document::undo() const {
    if (history->undo.empty()) {
        return;
    }
    history->redo.push_back(nodes.peek());
    batch([&] {
        nodes = history->undo.back();
        history->undo.pop_back();
        drop_missing(*this);
    });
}

void Document::redo() const {
    if (history->redo.empty()) {
        return;
    }
    history->undo.push_back(nodes.peek());
    batch([&] {
        nodes = history->redo.back();
        history->redo.pop_back();
        drop_missing(*this);
    });
}

// --- Editing ---

int Document::add(Node n) const {
    n.id = (*next_id)++;
    if (n.name.empty()) {
        int count = 1;
        for (const Node& other : nodes.peek()) {
            count += other.kind == n.kind ? 1 : 0;
        }
        n.name = std::format("{} {}", kind_name(n.kind), count);
    }
    edit([&](std::vector<Node>& all) { all.push_back(n); });
    return n.id;
}

void Document::update(int id, const std::function<void(Node&)>& change) const {
    edit([&](std::vector<Node>& all) {
        for (Node& n : all) {
            if (n.id == id) {
                change(n);
            }
        }
    });
}

void Document::update_selected(const std::function<void(Node&)>& change) const {
    const std::vector<int> ids = selection.peek();
    edit([&](std::vector<Node>& all) {
        for (Node& n : all) {
            if (std::ranges::contains(ids, n.id)) {
                change(n);
            }
        }
    });
}

std::vector<int> Document::selection_roots() const {
    std::vector<int> roots;
    const std::vector<int> chosen = selection.peek();
    for (const Node& n : nodes.peek()) { // tree order
        if (std::ranges::contains(chosen, n.id) &&
            !std::ranges::any_of(chosen, [&](int other) { return other != n.id && is_ancestor(other, n.id); })) {
            roots.push_back(n.id);
        }
    }
    return roots;
}

void Document::transform(const std::vector<int>& ids, Rect from, Rect to) const {
    const float sx = from.width > 0 ? to.width / from.width : 1;
    const float sy = from.height > 0 ? to.height / from.height : 1;
    const bool scaled = std::abs(sx - 1) > 1e-4f || std::abs(sy - 1) > 1e-4f;
    std::vector<int> all_ids;
    const Children children = children_of(nodes.peek());
    for (const int id : ids) {
        if (!std::ranges::contains(all_ids, id)) {
            collect_subtree(children, id, all_ids);
        }
    }
    edit([&](std::vector<Node>& all) {
        for (Node& n : all) {
            if (!std::ranges::contains(all_ids, n.id)) {
                continue;
            }
            n.x = to.x + (n.x - from.x) * sx;
            n.y = to.y + (n.y - from.y) * sy;
            if (n.kind == Kind::line) {
                n.w *= sx;
                n.h *= sy;
            } else {
                n.w = std::max(n.w * sx, 1.f);
                n.h = std::max(n.h * sy, 1.f);
            }
            // A resized auto-layout frame keeps the size it was given.
            if (scaled && n.auto_layout && std::ranges::contains(ids, n.id)) {
                n.hug = false;
            }
        }
    });
}

void Document::delete_selected() const {
    if (selection.peek().empty()) {
        return;
    }
    checkpoint();
    std::vector<int> doomed;
    const Children children = children_of(nodes.peek());
    for (const int id : selection.peek()) {
        collect_subtree(children, id, doomed);
    }
    batch([&] {
        edit([&](std::vector<Node>& all) { std::erase_if(all, [&](const Node& n) { return std::ranges::contains(doomed, n.id); }); });
        selection = std::vector<int>{};
        editing_text = 0;
    });
}

namespace {

// Copies of the subtrees under `roots` (tree order), with new ids from `next_id`. The copied
// roots keep their parent's id; `new_roots` gets their new ids.
std::vector<Node> copy_subtrees(const std::vector<Node>& nodes, const std::vector<int>& roots, int& next_id,
                                std::vector<int>& new_roots) {
    const Children children = children_of(nodes);
    std::vector<int> ids;
    for (const int r : roots) {
        collect_subtree(children, r, ids);
    }
    std::unordered_map<int, int> remap;
    for (const int id : ids) {
        remap[id] = next_id++;
    }
    const std::unordered_map<int, std::size_t> index = index_of(nodes);
    std::vector<Node> copies;
    for (const int id : ids) {
        Node n = nodes[index.at(id)];
        n.id = remap[id];
        if (const auto it = remap.find(n.parent); it != remap.end()) {
            n.parent = it->second;
        }
        copies.push_back(n);
    }
    for (const int r : roots) {
        new_roots.push_back(remap[r]);
    }
    return copies;
}

} // namespace

void Document::duplicate_selected() const {
    const std::vector<int> roots = selection_roots();
    if (roots.empty()) {
        return;
    }
    checkpoint();
    std::vector<int> new_roots;
    std::vector<Node> copies = copy_subtrees(nodes.peek(), roots, *next_id, new_roots);
    for (Node& n : copies) {
        n.x += 16;
        n.y += 16;
        if (std::ranges::contains(new_roots, n.id)) {
            n.name += " copy";
        }
    }
    batch([&] {
        edit([&](std::vector<Node>& all) { all.insert(all.end(), copies.begin(), copies.end()); });
        selection = new_roots;
    });
}

void Document::copy() const {
    const std::vector<int> roots = selection_roots();
    const Children children = children_of(nodes.peek());
    std::vector<int> ids;
    for (const int r : roots) {
        collect_subtree(children, r, ids);
    }
    clipboard->clear();
    for (const Node& n : nodes.peek()) {
        if (std::ranges::contains(ids, n.id)) {
            clipboard->push_back(n);
        }
    }
}

void Document::paste() const {
    if (clipboard->empty()) {
        return;
    }
    checkpoint();
    // Roots: copied layers whose parents weren't copied.
    std::vector<int> roots;
    for (const Node& n : *clipboard) {
        if (!std::ranges::any_of(*clipboard, [&](const Node& other) { return other.id == n.parent; })) {
            roots.push_back(n.id);
        }
    }
    std::vector<int> new_roots;
    std::vector<Node> copies = copy_subtrees(*clipboard, roots, *next_id, new_roots);
    const std::vector<Node>& existing = nodes.peek();
    for (Node& n : copies) {
        n.x += 24;
        n.y += 24;
        if (std::ranges::contains(new_roots, n.id) &&
            !std::ranges::any_of(existing, [&](const Node& e) { return e.id == n.parent; })) {
            n.parent = 0;
        }
    }
    batch([&] {
        edit([&](std::vector<Node>& all) { all.insert(all.end(), copies.begin(), copies.end()); });
        selection = new_roots;
    });
    for (Node& n : *clipboard) { // pasting again cascades
        n.x += 24;
        n.y += 24;
    }
}

void Document::select_all() const {
    std::vector<int> ids;
    for (const Node& n : nodes.peek()) {
        if (n.parent == 0 && n.visible && !n.locked) {
            ids.push_back(n.id);
        }
    }
    selection = ids;
}

namespace {

// Moves the selected layers among their siblings: one step (or all the way) toward the front
// (+1) or back (-1).
void restack(const Document& doc, int direction, bool all_the_way) {
    const std::vector<int> ids = doc.selection.peek();
    if (ids.empty()) {
        return;
    }
    doc.checkpoint();
    std::vector<Node> all = doc.nodes.peek();
    Children children = children_of(all);
    const auto chosen = [&](int id) { return std::ranges::contains(ids, id); };
    for (auto& [parent, list] : children) {
        if (all_the_way) {
            if (direction > 0) {
                std::ranges::stable_partition(list, [&](int id) { return !chosen(id); });
            } else {
                std::ranges::stable_partition(list, chosen);
            }
        } else if (direction > 0) {
            for (std::size_t i = list.size(); i-- > 1;) {
                if (chosen(list[i - 1]) && !chosen(list[i])) {
                    std::swap(list[i - 1], list[i]);
                }
            }
        } else {
            for (std::size_t i = 0; i + 1 < list.size(); ++i) {
                if (chosen(list[i + 1]) && !chosen(list[i])) {
                    std::swap(list[i], list[i + 1]);
                }
            }
        }
    }
    doc.set_nodes(flatten(all, children));
}

// Puts a new container (already in `all`, with its parent set) where the topmost of `roots`
// is among its siblings, and moves `ordered` (the same layers, in their new order) into it.
void wrap(const Document& doc, std::vector<Node> all, int container, const std::vector<int>& roots,
          const std::vector<int>& ordered) {
    const std::unordered_map<int, std::size_t> index = index_of(all);
    const int parent = all[index.at(container)].parent;
    Children children = children_of(all);
    std::vector<int>& siblings = children[parent];
    std::erase(siblings, container);
    std::size_t at = siblings.size();
    for (std::size_t i = 0; i < siblings.size(); ++i) {
        if (siblings[i] == roots.back()) {
            at = i + 1;
        }
    }
    siblings.insert(siblings.begin() + static_cast<std::ptrdiff_t>(at), container);
    for (auto& [p, list] : children) {
        if (p != container) {
            std::erase_if(list, [&](int id) { return std::ranges::contains(roots, id); });
        }
    }
    children[container] = ordered;
    for (Node& n : all) {
        if (std::ranges::contains(roots, n.id)) {
            n.parent = container;
        }
    }
    batch([&] {
        doc.set_nodes(flatten(all, children));
        doc.selection = std::vector<int>{container};
    });
}

} // namespace

void Document::bring_forward() const { restack(*this, 1, false); }
void Document::send_backward() const { restack(*this, -1, false); }
void Document::bring_to_front() const { restack(*this, 1, true); }
void Document::send_to_back() const { restack(*this, -1, true); }

void Document::align(Align how) const {
    const std::optional<Rect> area = untrack([&] { return selection_bounds(); });
    if (!area) {
        return;
    }
    checkpoint();
    for (const int id : selection_roots()) {
        const std::optional<Node> n = untrack([&] { return node(id); });
        if (!n) {
            continue;
        }
        const Rect b = n->bounds();
        float dx = 0;
        float dy = 0;
        switch (how) {
        case Align::left: dx = area->x - b.x; break;
        case Align::center_x: dx = area->x + area->width / 2 - (b.x + b.width / 2); break;
        case Align::right: dx = area->max_x() - b.max_x(); break;
        case Align::top: dy = area->y - b.y; break;
        case Align::center_y: dy = area->y + area->height / 2 - (b.y + b.height / 2); break;
        case Align::bottom: dy = area->max_y() - b.max_y(); break;
        }
        transform({id}, b, {b.x + dx, b.y + dy, b.width, b.height});
    }
}

void Document::nudge(float dx, float dy) const {
    const std::vector<int> roots = selection_roots();
    const std::optional<Rect> area = untrack([&] { return selection_bounds(); });
    if (roots.empty() || !area) {
        return;
    }
    checkpoint();
    transform(roots, *area, {area->x + dx, area->y + dy, area->width, area->height});
}

void Document::reparent(const std::vector<int>& ids, int parent, std::optional<std::size_t> index) const {
    std::vector<Node> all = nodes.peek();
    Children children = children_of(all);
    for (auto& [p, list] : children) {
        std::erase_if(list, [&](int id) { return std::ranges::contains(ids, id); });
    }
    std::vector<int>& target = children[parent];
    const std::size_t at = std::min(index.value_or(target.size()), target.size());
    target.insert(target.begin() + static_cast<std::ptrdiff_t>(at), ids.begin(), ids.end());
    for (Node& n : all) {
        if (std::ranges::contains(ids, n.id)) {
            n.parent = parent;
        }
    }
    set_nodes(flatten(all, children));
}

void Document::group_selection() const {
    const std::vector<int> roots = selection_roots();
    if (roots.empty()) {
        return;
    }
    checkpoint();
    std::vector<Node> all = nodes.peek();
    const std::unordered_map<int, std::size_t> index = index_of(all);
    Node group;
    group.kind = Kind::group;
    group.id = (*next_id)++;
    group.parent = all[index.at(roots.back())].parent;
    group.has_fill = false;
    int count = 1;
    for (const Node& n : all) {
        count += n.kind == Kind::group ? 1 : 0;
    }
    group.name = std::format("Group {}", count);
    all.push_back(group);
    wrap(*this, std::move(all), group.id, roots, roots);
}

void Document::ungroup_selection() const {
    std::vector<int> groups;
    for (const Node& n : selected()) {
        if (n.kind == Kind::group) {
            groups.push_back(n.id);
        }
    }
    if (groups.empty()) {
        return;
    }
    checkpoint();
    std::vector<Node> all = nodes.peek();
    Children children = children_of(all);
    std::vector<int> released;
    for (const int g : groups) {
        const std::unordered_map<int, std::size_t> index = index_of(all);
        const int parent = all[index.at(g)].parent;
        const std::vector<int> kids = children[g];
        std::vector<int>& siblings = children[parent];
        const auto at = std::ranges::find(siblings, g);
        const std::ptrdiff_t position = at - siblings.begin();
        siblings.erase(at);
        siblings.insert(siblings.begin() + position, kids.begin(), kids.end());
        children.erase(g);
        for (Node& n : all) {
            if (std::ranges::contains(kids, n.id)) {
                n.parent = parent;
            }
        }
        std::erase_if(all, [&](const Node& n) { return n.id == g; });
        released.insert(released.end(), kids.begin(), kids.end());
    }
    batch([&] {
        set_nodes(flatten(all, children));
        selection = released;
    });
}

void Document::add_auto_layout() const {
    const std::vector<int> roots = selection_roots();
    if (roots.empty()) {
        return;
    }
    const std::optional<Node> first = untrack([&] { return node(roots.front()); });
    if (roots.size() == 1 && first && first->kind == Kind::frame) {
        if (!first->auto_layout) {
            checkpoint();
            update(first->id, [](Node& n) {
                n.auto_layout = true;
                n.hug = true;
            });
        }
        return;
    }
    // Wrap the selection in a new auto-layout frame, in reading order along its direction.
    const std::optional<Rect> area = untrack([&] { return selection_bounds(); });
    if (!area) {
        return;
    }
    checkpoint();
    const bool row = area->width > area->height;
    std::vector<int> ordered = roots;
    std::ranges::sort(ordered, [&](int a, int b) {
        const Rect ra = untrack([&] { return node(a); })->bounds();
        const Rect rb = untrack([&] { return node(b); })->bounds();
        return row ? ra.x < rb.x : ra.y < rb.y;
    });
    std::vector<Node> all = nodes.peek();
    const std::unordered_map<int, std::size_t> index = index_of(all);
    Node frame;
    frame.kind = Kind::frame;
    frame.id = (*next_id)++;
    frame.parent = all[index.at(roots.back())].parent;
    frame.x = area->x - 16;
    frame.y = area->y - 16;
    frame.w = area->width + 32;
    frame.h = area->height + 32;
    frame.has_fill = false;
    frame.clips = false;
    frame.auto_layout = true;
    frame.horizontal = row;
    int count = 1;
    for (const Node& n : all) {
        count += n.kind == Kind::frame ? 1 : 0;
    }
    frame.name = std::format("Frame {}", count);
    all.push_back(frame);
    wrap(*this, std::move(all), frame.id, roots, ordered);
}

// --- View ---

void Document::zoom_at(Point screen, float factor) const {
    const float z = zoom.peek();
    const float next = std::clamp(z * factor, 0.02f, 64.f);
    const Point anchor = to_canvas(screen);
    batch([&] {
        zoom = next;
        pan = Point{screen.x - anchor.x * next, screen.y - anchor.y * next};
    });
}

void Document::zoom_to(float next, Size viewport) const {
    zoom_at({viewport.width / 2, viewport.height / 2}, next / zoom.peek());
}

void Document::zoom_to_fit(Size viewport) const {
    std::optional<Rect> all;
    for (const Node& n : nodes.peek()) {
        all = all ? union_of(*all, n.bounds()) : n.bounds();
    }
    if (!all || viewport.width <= 0 || viewport.height <= 0) {
        return;
    }
    constexpr float kMargin = 80;
    const float z = std::clamp(std::min((viewport.width - 2 * kMargin) / std::max(all->width, 1.f),
                                        (viewport.height - 2 * kMargin) / std::max(all->height, 1.f)),
                               0.02f, 4.f);
    batch([&] {
        zoom = z;
        pan = Point{viewport.width / 2 - (all->x + all->width / 2) * z, viewport.height / 2 - (all->y + all->height / 2) * z};
    });
}

void Document::load_sample() const {
    const auto shape = [&](Kind kind, int parent, float x, float y, float w, float h, Color fill, std::string name = {}) {
        Node n;
        n.kind = kind;
        n.parent = parent;
        n.x = x;
        n.y = y;
        n.w = w;
        n.h = h;
        n.fill = fill;
        n.name = std::move(name);
        return n;
    };
    const auto text = [&](int parent, float x, float y, std::string content, float size, Color color, bool bold) {
        Node n;
        n.kind = Kind::text;
        n.parent = parent;
        n.x = x;
        n.y = y;
        n.text = std::move(content);
        n.font_size = size;
        n.bold = bold;
        n.fill = color;
        n.name = n.text.substr(0, 24);
        const Size s = estimate_text_size(n.text, size, bold);
        n.w = s.width;
        n.h = s.height;
        return n;
    };
    batch([&] {
        // A phone screen.
        const int phone = add(shape(Kind::frame, 0, 0, 0, 375, 760, Color::white, "iPhone - Home"));
        add(shape(Kind::rectangle, phone, 0, 0, 375, 220, Color::hex(0x6C5CE7), "Header"));
        add(text(phone, 24, 64, "Good morning,", 18, Color::hex(0xE0DDFF), false));
        add(text(phone, 24, 92, "Alex", 34, Color::white, true));
        Node avatar = shape(Kind::ellipse, phone, 299, 64, 52, 52, Color::hex(0xFDCB6E), "Avatar");
        avatar.has_stroke = true;
        avatar.stroke = Color::white;
        avatar.stroke_width = 3;
        add(avatar);

        // A card, grouped.
        Node card = shape(Kind::rectangle, phone, 24, 180, 327, 140, Color::white, "Card background");
        card.radius = 18;
        card.has_stroke = true;
        card.stroke = Color::hex(0xE6E6EF);
        const int card_bg = add(card);
        const int focus_label = add(text(phone, 44, 200, "Today's focus", 15, Color::hex(0x8A8AA0), false));
        const int focus = add(text(phone, 44, 226, "Ship the canvas", 24, Color::hex(0x1E1E2E), true));
        Node track = shape(Kind::rectangle, phone, 44, 280, 287, 10, Color::hex(0xEEEAFE), "Progress track");
        track.radius = 5;
        const int track_id = add(track);
        Node bar = shape(Kind::rectangle, phone, 44, 280, 190, 10, Color::hex(0x6C5CE7), "Progress");
        bar.radius = 5;
        const int bar_id = add(bar);
        selection = std::vector<int>{card_bg, focus_label, focus, track_id, bar_id};
        group_selection();
        update(selection.peek().front(), [](Node& n) { n.name = "Focus card"; });

        // Tasks: an auto-layout column of auto-layout rows.
        Node list = shape(Kind::frame, phone, 24, 344, 327, 10, Color::white, "Tasks");
        list.has_fill = false;
        list.clips = false;
        list.auto_layout = true;
        list.padding = 0;
        list.gap = 12;
        list.align = CrossAlign::stretch;
        const int tasks = add(list);
        const char* titles[] = {"Review pull request", "Design the inspector", "Write release notes"};
        for (int i = 0; i < 3; ++i) {
            Node row = shape(Kind::frame, tasks, 0, 0, 327, 60, Color::hex(0xF6F5FB), std::format("Task {}", i + 1));
            row.radius = 14;
            row.auto_layout = true;
            row.horizontal = true;
            row.gap = 14;
            row.padding = 18;
            row.align = CrossAlign::center;
            const int r = add(row);
            add(shape(Kind::ellipse, r, 0, 0, 22, 22, i == 0 ? Color::hex(0x00B894) : Color::hex(0xDFE6E9),
                      std::format("Check {}", i + 1)));
            add(text(r, 0, 0, titles[i], 16, Color::hex(0x2D3436), false));
        }

        // A button: a hugging auto-layout frame around its label.
        Node button = shape(Kind::frame, phone, 24, 650, 327, 56, Color::hex(0x6C5CE7), "Button");
        button.radius = 28;
        button.auto_layout = true;
        button.horizontal = true;
        button.padding = 16;
        button.align = CrossAlign::center;
        const int button_id = add(button);
        add(text(button_id, 0, 0, "+  New task", 18, Color::white, true));

        // A second frame: a color palette with an auto-layout row of swatches.
        const int palette = add(shape(Kind::frame, 0, 460, 0, 400, 180, Color::white, "Palette"));
        add(text(palette, 484, 24, "Colors", 22, Color::hex(0x1E1E2E), true));
        Node swatches = shape(Kind::frame, palette, 484, 80, 10, 10, Color::white, "Swatches");
        swatches.has_fill = false;
        swatches.auto_layout = true;
        swatches.horizontal = true;
        swatches.padding = 0;
        swatches.gap = 12;
        const int row = add(swatches);
        const std::uint32_t colors[] = {0x6C5CE7, 0x00B894, 0xFDCB6E, 0xE17055, 0x0984E3};
        for (int i = 0; i < 5; ++i) {
            Node s = shape(Kind::rectangle, row, 0, 0, 56, 56, Color::hex(colors[i]), std::format("Swatch {}", i + 1));
            s.radius = 12;
            add(s);
        }
        selection = std::vector<int>{};
    });
    history->undo.clear();
    history->redo.clear();
}

std::string to_hex(Color color) {
    const auto channel = [](float v) { return static_cast<int>(std::lround(std::clamp(v, 0.f, 1.f) * 255)); };
    return std::format("{:02X}{:02X}{:02X}", channel(color.r), channel(color.g), channel(color.b));
}

std::optional<Color> from_hex(std::string text) {
    std::erase(text, '#');
    std::erase(text, ' ');
    if (text.size() == 3) {
        text = {text[0], text[0], text[1], text[1], text[2], text[2]};
    }
    if (text.size() != 6) {
        return std::nullopt;
    }
    unsigned value = 0;
    for (const char c : text) {
        value <<= 4;
        if (c >= '0' && c <= '9') {
            value |= static_cast<unsigned>(c - '0');
        } else if (c >= 'a' && c <= 'f') {
            value |= static_cast<unsigned>(c - 'a' + 10);
        } else if (c >= 'A' && c <= 'F') {
            value |= static_cast<unsigned>(c - 'A' + 10);
        } else {
            return std::nullopt;
        }
    }
    return Color::hex(value);
}

} // namespace design
