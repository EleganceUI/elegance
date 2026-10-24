// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef DESIGN_MODEL_H
#define DESIGN_MODEL_H

// The document: a tree of layers (frames, groups, shapes, text) on an infinite canvas, the
// selection, the view (pan and zoom), the current tool, and undo history.
//
// Layers live in one list in tree order: each parent comes right before its children (so the
// list is also back-to-front drawing order). Positions are absolute canvas coordinates. After
// every change, normalize() puts the list back in tree order, lays out auto-layout frames, and
// fits groups around their children.

#include <elg/elegence.h>

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace design {

enum class Tool { select, frame, rectangle, ellipse, line, text, hand };
enum class Kind { frame, group, rectangle, ellipse, line, text };
// How an auto-layout frame lines children up across its direction.
enum class CrossAlign { start, center, end, stretch };

struct Node {
    int id = 0;
    int parent = 0; // 0: on the page
    Kind kind = Kind::rectangle;
    std::string name;
    // In canvas units. A line runs from (x, y) to (x + w, y + h); other kinds keep w, h >= 0.
    float x = 0;
    float y = 0;
    float w = 0;
    float h = 0;
    bool has_fill = true;
    elg::Color fill = elg::Color::hex(0xD9D9D9);
    bool has_stroke = false;
    elg::Color stroke = elg::Color::hex(0x000000);
    float stroke_width = 1;
    float radius = 0;
    float opacity = 1;
    bool visible = true;
    bool locked = false;
    std::string text;     // text layers
    float font_size = 24; // text layers
    bool bold = false;    // text layers
    bool clips = true;    // frames: content outside them is hidden
    // Auto layout (frames).
    bool auto_layout = false;
    bool horizontal = false; // a row; else a column
    float gap = 10;
    float padding = 16;
    CrossAlign align = CrossAlign::start;
    bool hug = true; // the frame fits its content; else it keeps its size
    friend bool operator==(const Node&, const Node&) = default;

    elg::Rect bounds() const; // normalized (lines: their bounding box)
    bool is_container() const { return kind == Kind::frame || kind == Kind::group; }
};

// A node's text box size: estimated from the characters (the app can't measure text yet).
elg::Size estimate_text_size(const std::string& text, float font_size, bool bold);

// The layers' tree: each parent's children, back to front (0: the page's).
using Children = std::map<int, std::vector<int>>;
Children children_of(const std::vector<Node>& nodes);
// The list in tree order from `children`.
std::vector<Node> flatten(const std::vector<Node>& nodes, const Children& children);
// Puts layers in tree order, lays out auto-layout frames (except layers in `floating`, being
// dragged), and fits groups to their children. Removes empty groups.
std::vector<Node> normalize(std::vector<Node> nodes, const std::set<int>& floating = {});

struct History {
    std::vector<std::vector<Node>> undo;
    std::vector<std::vector<Node>> redo;
};

struct Document {
    elg::State<std::vector<Node>> nodes;
    elg::State<std::vector<int>> selection;
    elg::State<Tool> tool{Tool::select};
    elg::State<float> zoom{1.f};
    elg::State<elg::Point> pan{elg::Point{0, 0}}; // screen = canvas * zoom + pan
    elg::State<int> hovered{0};
    elg::State<int> editing_text{0}; // a text layer being typed into, or 0
    elg::State<int> drop_target{0};  // the frame a drag would drop into (highlighted)
    elg::State<elg::Color> background{elg::Color::hex(0x1E1E1E)};
    elg::State<bool> show_grid{true};
    elg::State<bool> snapping{true};
    elg::State<std::string> name{"Untitled"};
    std::shared_ptr<History> history = std::make_shared<History>();
    std::shared_ptr<std::vector<Node>> clipboard = std::make_shared<std::vector<Node>>();
    std::shared_ptr<int> next_id = std::make_shared<int>(1);
    // Layers being dragged: auto layout leaves them where the pointer puts them.
    std::shared_ptr<std::set<int>> floating = std::make_shared<std::set<int>>();

    // Writes `next` normalized.
    void set_nodes(std::vector<Node> next) const;
    void edit(const std::function<void(std::vector<Node>&)>& change) const;
    // Lays out again (after `floating` changed).
    void relayout() const;

    // --- Reading ---
    std::optional<Node> node(int id) const; // tracked
    bool is_selected(int id) const;         // tracked
    std::vector<Node> selected() const;     // tracked, in tree order
    std::optional<elg::Rect> selection_bounds() const;
    std::vector<int> descendants(int id) const; // untracked
    bool is_ancestor(int ancestor, int id) const;
    int depth(int id) const;

    // The deepest visible, unlocked layer at a canvas point (groups aren't hit themselves).
    int hit(elg::Point canvas) const;
    // What a click on `id` selects: the outermost group around it, unless already inside it.
    int click_target(int id) const;
    // The topmost frame at a canvas point that could take `moving` (not one of them, not
    // inside them), or 0 for the page.
    int frame_at(elg::Point canvas, const std::vector<int>& moving = {}) const;

    elg::Point to_canvas(elg::Point screen) const;
    elg::Point to_screen(elg::Point canvas) const;

    // --- Editing (each undoable) ---
    void checkpoint() const; // remembers the current layers for undo; call before a change
    void undo() const;
    void redo() const;

    // Adds `node` on top of its parent's children; fills in id and name; returns the id.
    int add(Node node) const;
    void update(int id, const std::function<void(Node&)>& change) const;
    void update_selected(const std::function<void(Node&)>& change) const;
    // Moves (and scales) the given layers and everything inside them so their bounds go from
    // `from` to `to`.
    void transform(const std::vector<int>& ids, elg::Rect from, elg::Rect to) const;
    void delete_selected() const;
    void duplicate_selected() const;
    void copy() const;
    void paste() const;
    void select_all() const;
    void bring_forward() const;
    void send_backward() const;
    void bring_to_front() const;
    void send_to_back() const;
    enum class Align { left, center_x, right, top, center_y, bottom };
    void align(Align how) const;
    void nudge(float dx, float dy) const;
    // Moves layers into `parent` (0: the page): on top, or at `index` among its children.
    void reparent(const std::vector<int>& ids, int parent, std::optional<std::size_t> index = std::nullopt) const;
    void group_selection() const;
    void ungroup_selection() const;
    // Shift+A: turns on auto layout for a selected frame, or wraps the selection in one.
    void add_auto_layout() const;
    // The selected layers whose parents aren't selected too.
    std::vector<int> selection_roots() const;

    // --- View ---
    void zoom_at(elg::Point screen, float factor) const;
    void zoom_to(float zoom, elg::Size viewport) const; // about the viewport's middle
    void zoom_to_fit(elg::Size viewport) const;

    // A starting document: a couple of frames with content, one with auto layout.
    void load_sample() const;
};

std::string to_hex(elg::Color color);
std::optional<elg::Color> from_hex(std::string text);

} // namespace design

#endif // DESIGN_MODEL_H
