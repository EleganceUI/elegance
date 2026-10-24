// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include "canvas.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

namespace design {

using namespace elg;

namespace {

constexpr Color kSelection = Color::hex(0x0D99FF);
constexpr Color kGuide = Color::hex(0xF24822);
constexpr float kHandle = 8;      // handle squares, in screen points
constexpr float kSnap = 6;        // snapping distance, in screen points

// What the canvas shows while a gesture is in progress (drawn, so reactive).
struct Overlay {
    State<std::optional<Rect>> marquee;                      // screen
    State<std::vector<std::pair<Point, Point>>> guides;      // canvas
};

bool inside(Rect r, Point p) {
    return p.x >= r.x && p.y >= r.y && p.x <= r.max_x() && p.y <= r.max_y();
}

bool intersects(Rect a, Rect b) {
    return a.x <= b.max_x() && b.x <= a.max_x() && a.y <= b.max_y() && b.y <= a.max_y();
}

Rect normalized(Point a, Point b) {
    return {std::min(a.x, b.x), std::min(a.y, b.y), std::abs(b.x - a.x), std::abs(b.y - a.y)};
}

Rect to_screen(const Document& doc, Rect r) {
    const float z = doc.zoom();
    const Point p = doc.pan();
    return {r.x * z + p.x, r.y * z + p.y, r.width * z, r.height * z};
}

// The 8 handles around a screen rect: corners and edge midpoints, clockwise from the top-left.
std::vector<Point> handle_points(Rect r) {
    const float cx = r.x + r.width / 2;
    const float cy = r.y + r.height / 2;
    return {{r.x, r.y}, {cx, r.y}, {r.max_x(), r.y}, {r.max_x(), cy},
            {r.max_x(), r.max_y()}, {cx, r.max_y()}, {r.x, r.max_y()}, {r.x, cy}};
}

Cursor handle_cursor(int handle) {
    switch (handle) {
    case 0:
    case 4: return Cursor::resize_nwse;
    case 2:
    case 6: return Cursor::resize_nesw;
    case 1:
    case 5: return Cursor::resize_vertical;
    default: return Cursor::resize_horizontal;
    }
}

// --- Drawing ---

void draw_grid(GraphicsContext& g, const Document& doc) {
    const float z = doc.zoom();
    if (!doc.show_grid() || z < 0.3f) {
        return;
    }
    float step = 20;
    while (step * z < 16) {
        step *= 5;
    }
    const Point pan = doc.pan();
    const Size size = g.size();
    const float screen_step = step * z;
    const float x0 = std::fmod(pan.x, screen_step);
    const float y0 = std::fmod(pan.y, screen_step);
    const Color bg = doc.background();
    const bool dark = bg.r + bg.g + bg.b < 1.5f;
    const Color dot = dark ? Color::white.opacity(0.09f) : Color::black.opacity(0.1f);
    for (float y = y0 < 0 ? y0 + screen_step : y0; y < size.height; y += screen_step) {
        for (float x = x0 < 0 ? x0 + screen_step : x0; x < size.width; x += screen_step) {
            g.fill_rect({x - 1, y - 1, 2, 2}, dot, 1);
        }
    }
}

void draw_node(GraphicsContext& g, const Document& doc, const Node& n) {
    const float z = doc.zoom();
    const Rect r = to_screen(doc, n.bounds());
    const Color fill = n.has_fill ? n.fill.opacity(n.opacity) : Color::clear;
    const Color stroke = n.has_stroke ? n.stroke.opacity(n.opacity) : Color::clear;
    const float stroke_width = n.has_stroke ? std::max(n.stroke_width * z, 0.5f) : 0.f;
    switch (n.kind) {
    case Kind::frame:
    case Kind::rectangle:
        g.draw_rect(r, CornerRadii::all(n.radius * z), fill, stroke, stroke_width);
        break;
    case Kind::ellipse: {
        Path path;
        path.add_ellipse(r);
        if (n.has_fill) {
            g.fill_path(path, fill);
        }
        if (n.has_stroke) {
            g.stroke_path(path, stroke, {.width = stroke_width});
        }
        break;
    }
    case Kind::line: {
        const Point a = doc.to_screen({n.x, n.y});
        const Point b = doc.to_screen({n.x + n.w, n.y + n.h});
        Path path;
        path.move_to(a).line_to(b);
        g.stroke_path(path, n.has_stroke ? stroke : fill, {.width = std::max(n.stroke_width * z, 1.f), .cap = LineCap::round});
        break;
    }
    case Kind::text:  // drawn by the text overlay
    case Kind::group: // only its children
        break;
    }
}

void draw_outline(GraphicsContext& g, const Document& doc, const Node& n, float width) {
    if (n.kind == Kind::ellipse) {
        Path path;
        path.add_ellipse(to_screen(doc, n.bounds()));
        g.stroke_path(path, kSelection, {.width = width});
        return;
    }
    if (n.kind == Kind::line) {
        Path path;
        path.move_to(doc.to_screen({n.x, n.y})).line_to(doc.to_screen({n.x + n.w, n.y + n.h}));
        g.stroke_path(path, kSelection, {.width = width});
        return;
    }
    const Rect r = to_screen(doc, n.bounds());
    g.stroke_rect(r.inset(-width), kSelection, width);
}

// Draws `id`'s subtree: a frame clips what's inside it; a group draws only its children.
void draw_tree(GraphicsContext& g, const Document& doc, const std::vector<Node>& all, const Children& children,
               const std::unordered_map<int, std::size_t>& index, int id) {
    const Node& n = all[index.at(id)];
    if (!n.visible) {
        return;
    }
    draw_node(g, doc, n);
    const auto it = children.find(id);
    if (it == children.end() || it->second.empty()) {
        return;
    }
    const bool clip = n.kind == Kind::frame && n.clips;
    if (clip) {
        g.push_clip(to_screen(doc, n.bounds()), CornerRadii::all(n.radius * doc.zoom()));
    }
    for (const int child : it->second) {
        draw_tree(g, doc, all, children, index, child);
    }
    if (clip) {
        g.pop_clip();
    }
}

void draw(GraphicsContext& g, const Document& doc, const Overlay& overlay) {
    const Size size = g.size();
    g.fill_rect({0, 0, size.width, size.height}, doc.background());
    draw_grid(g, doc);
    const std::vector<Node>& nodes = doc.nodes();
    const Children children = children_of(nodes);
    std::unordered_map<int, std::size_t> index;
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        index[nodes[i].id] = i;
    }
    for (const int id : children.at(0)) {
        draw_tree(g, doc, nodes, children, index, id);
    }
    // The frame a drag would drop into.
    if (const int target = doc.drop_target(); target != 0) {
        if (const std::optional<Node> n = doc.node(target)) {
            g.stroke_rect(to_screen(doc, n->bounds()).inset(-2), kSelection, 2);
        }
    }
    // Hover, then selection on top.
    const int hovered = doc.hovered();
    const std::vector<int>& selection = doc.selection();
    if (hovered != 0 && !std::ranges::contains(selection, hovered)) {
        if (const std::optional<Node> n = doc.node(hovered)) {
            draw_outline(g, doc, *n, 1.5f);
        }
    }
    for (const Node& n : nodes) {
        if (std::ranges::contains(selection, n.id)) {
            draw_outline(g, doc, n, 1);
        }
    }
    if (const std::optional<Rect> bounds = doc.selection_bounds(); bounds && doc.editing_text() == 0) {
        const Rect r = to_screen(doc, *bounds);
        if (selection.size() > 1) {
            g.stroke_rect(r.inset(-1), kSelection, 1);
        }
        for (const Point p : handle_points(r)) {
            g.draw_rect({p.x - kHandle / 2, p.y - kHandle / 2, kHandle, kHandle}, CornerRadii::all(1), Color::white, kSelection, 1);
        }
    }
    for (const auto& [a, b] : overlay.guides()) {
        Path path;
        path.move_to(doc.to_screen(a)).line_to(doc.to_screen(b));
        g.stroke_path(path, kGuide, {.width = 1});
    }
    if (const std::optional<Rect> m = overlay.marquee()) {
        g.draw_rect(*m, {}, kSelection.opacity(0.08f), kSelection, 1);
    }
}

// --- Interaction ---

void subtree(const Children& children, int id, std::vector<int>& out) {
    out.push_back(id);
    if (const auto it = children.find(id); it != children.end()) {
        for (const int child : it->second) {
            subtree(children, child, out);
        }
    }
}

// `base` with the subtrees under `roots` moved and scaled so their bounds go from `from` to
// `to`. Resized auto-layout frames among `roots` stop hugging their content.
std::vector<Node> transformed(std::vector<Node> base, const std::vector<int>& roots, Rect from, Rect to) {
    const float sx = from.width > 0 ? to.width / from.width : 1;
    const float sy = from.height > 0 ? to.height / from.height : 1;
    const bool scaled = std::abs(sx - 1) > 1e-4f || std::abs(sy - 1) > 1e-4f;
    const Children children = children_of(base);
    std::vector<int> ids;
    for (const int r : roots) {
        subtree(children, r, ids);
    }
    for (Node& n : base) {
        if (!std::ranges::contains(ids, n.id)) {
            continue;
        }
        n.x = to.x + (n.x - from.x) * sx;
        n.y = to.y + (n.y - from.y) * sy;
        if (n.kind == Kind::line) {
            n.w *= sx;
            n.h *= sy;
        } else if (scaled) {
            n.w = std::max(n.w * sx, 1.f);
            n.h = std::max(n.h * sy, 1.f);
        }
        if (scaled && n.auto_layout && std::ranges::contains(roots, n.id)) {
            n.hug = false;
        }
    }
    return base;
}

Rect bounds_of(const std::vector<Node>& nodes, const std::vector<int>& ids) {
    std::optional<Rect> area;
    for (const Node& n : nodes) {
        if (std::ranges::contains(ids, n.id)) {
            const Rect b = n.bounds();
            if (!area) {
                area = b;
            } else {
                const float x = std::min(area->x, b.x);
                const float y = std::min(area->y, b.y);
                area = Rect{x, y, std::max(area->max_x(), b.max_x()) - x, std::max(area->max_y(), b.max_y()) - y};
            }
        }
    }
    return area.value_or(Rect{});
}

class CanvasHandler final : public EventHandler {
public:
    CanvasHandler(Document doc, Overlay overlay) : doc_(std::move(doc)), overlay_(std::move(overlay)) {}

    bool focusable() const override { return true; }

    EventResult hover(bool inside) override {
        if (!inside) {
            doc_.hovered = 0;
            set_cursor(Cursor::arrow);
        }
        return {};
    }

    EventResult pointer(const PointerEvent& event, Rect frame) override {
        frame_ = frame;
        const Point local{event.location.x - frame.x, event.location.y - frame.y};
        switch (event.kind) {
        case PointerEvent::Kind::hover: on_hover(local); return {.consumed = true};
        case PointerEvent::Kind::down: return on_down(event, local);
        case PointerEvent::Kind::move: on_move(event, local); return {.consumed = true, .redraw = true};
        case PointerEvent::Kind::up:
        case PointerEvent::Kind::cancel: on_up(local); return {.consumed = true, .redraw = true};
        }
        return {};
    }

    EventResult scroll(const ScrollEvent& event) override {
        const Point local{event.location.x - frame_.x, event.location.y - frame_.y};
        if (has(event.modifiers, KeyModifiers::control) || has(event.modifiers, KeyModifiers::super)) {
            doc_.zoom_at(local, std::exp(-event.dy * 0.0025f));
        } else {
            const Point p = doc_.pan.peek();
            doc_.pan = Point{p.x - event.dx, p.y - event.dy};
        }
        return {.consumed = true, .redraw = true};
    }

    EventResult key(const KeyEvent& event) override {
        if (event.key == Key::space) {
            space_ = event.kind == KeyEvent::Kind::down;
            set_cursor(space_ ? Cursor::move : Cursor::arrow);
            return {.consumed = true};
        }
        if (event.kind != KeyEvent::Kind::down) {
            return {};
        }
        const bool command = has(event.modifiers, KeyModifiers::control) || has(event.modifiers, KeyModifiers::super);
        const bool shift = has(event.modifiers, KeyModifiers::shift);
        const Size viewport{frame_.width, frame_.height};
        if (command) {
            switch (event.key) {
            case Key::z: shift ? doc_.redo() : doc_.undo(); break;
            case Key::y: doc_.redo(); break;
            case Key::d: doc_.duplicate_selected(); break;
            case Key::c: doc_.copy(); break;
            case Key::x:
                doc_.copy();
                doc_.delete_selected();
                break;
            case Key::v: doc_.paste(); break;
            case Key::a: doc_.select_all(); break;
            case Key::g: shift ? doc_.ungroup_selection() : doc_.group_selection(); break;
            case Key::digit0: doc_.zoom_to(1, viewport); break;
            default: return {};
            }
            return {.consumed = true, .redraw = true};
        }
        const float step = shift ? 10.f : 1.f;
        switch (event.key) {
        case Key::backspace:
        case Key::forward_delete: doc_.delete_selected(); break;
        case Key::escape:
            batch([&] {
                // Up a level: from a layer to the group or frame around it, then to nothing.
                const std::vector<int> roots = doc_.selection_roots();
                int parent = 0;
                if (roots.size() == 1) {
                    if (const std::optional<Node> n = untrack([&] { return doc_.node(roots.front()); })) {
                        parent = n->parent;
                    }
                }
                doc_.selection = parent != 0 ? std::vector<int>{parent} : std::vector<int>{};
                doc_.tool = Tool::select;
            });
            break;
        case Key::enter: {
            // Down a level: into the selected group or frame.
            const std::vector<int> roots = doc_.selection_roots();
            if (roots.size() == 1) {
                std::vector<int> kids;
                for (const Node& n : doc_.nodes.peek()) {
                    if (n.parent == roots.front()) {
                        kids.push_back(n.id);
                    }
                }
                if (!kids.empty()) {
                    doc_.selection = kids;
                }
            }
            break;
        }
        case Key::left: doc_.nudge(-step, 0); break;
        case Key::right: doc_.nudge(step, 0); break;
        case Key::up: doc_.nudge(0, -step); break;
        case Key::down: doc_.nudge(0, step); break;
        case Key::digit1:
            if (!shift) {
                return {};
            }
            doc_.zoom_to_fit(viewport);
            break;
        case Key::a:
            if (!shift) {
                return {};
            }
            doc_.add_auto_layout();
            break;
        case Key::v: doc_.tool = Tool::select; break;
        case Key::f: doc_.tool = Tool::frame; break;
        case Key::r: doc_.tool = Tool::rectangle; break;
        case Key::o: doc_.tool = Tool::ellipse; break;
        case Key::l: doc_.tool = Tool::line; break;
        case Key::t: doc_.tool = Tool::text; break;
        case Key::h: doc_.tool = Tool::hand; break;
        default: return {};
        }
        return {.consumed = true, .redraw = true};
    }

private:
    enum class Mode { none, pan, move, resize, create, marquee };

    void on_hover(Point local) {
        if (space_ || doc_.tool.peek() == Tool::hand) {
            set_cursor(Cursor::move);
            return;
        }
        if (doc_.tool.peek() != Tool::select) {
            doc_.hovered = 0;
            set_cursor(Cursor::crosshair);
            return;
        }
        if (const int handle = handle_at(local); handle >= 0) {
            set_cursor(handle_cursor(handle));
            doc_.hovered = 0;
            return;
        }
        set_cursor(Cursor::arrow);
        doc_.hovered = doc_.click_target(doc_.hit(doc_.to_canvas(local)));
    }

    // Which of the selection's handles is under `local`, or -1.
    int handle_at(Point local) const {
        if (doc_.editing_text.peek() != 0) {
            return -1;
        }
        const std::optional<Rect> bounds = untrack([&] { return doc_.selection_bounds(); });
        if (!bounds) {
            return -1;
        }
        const std::vector<Point> points = handle_points(untrack([&] { return to_screen(doc_, *bounds); }));
        for (std::size_t i = 0; i < points.size(); ++i) {
            if (std::abs(local.x - points[i].x) <= kHandle && std::abs(local.y - points[i].y) <= kHandle) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    // Remembers the layers as they are, to transform from while dragging.
    void begin_transform() {
        base_ = doc_.nodes.peek();
        roots_ = doc_.selection_roots();
        base_bounds_ = bounds_of(base_, roots_);
    }

    EventResult on_down(const PointerEvent& event, Point local) {
        start_ = local;
        start_canvas_ = doc_.to_canvas(local);
        start_pan_ = doc_.pan.peek();
        shift_ = has(event.modifiers, KeyModifiers::shift);
        const Tool tool = doc_.tool.peek();

        if (event.button == MouseButton::middle || space_ || tool == Tool::hand) {
            mode_ = Mode::pan;
            return {.consumed = true};
        }
        if (event.button != MouseButton::left) {
            return {};
        }
        if (doc_.editing_text.peek() != 0) {
            doc_.editing_text = 0; // clicking elsewhere ends typing
        }
        // New layers go into the frame under the pointer.
        const int parent = doc_.frame_at(start_canvas_);
        if (tool == Tool::text) {
            doc_.checkpoint();
            Node n;
            n.kind = Kind::text;
            n.parent = parent;
            n.x = start_canvas_.x;
            n.y = start_canvas_.y;
            n.fill = parent != 0 ? Color::hex(0x1E1E2E) : Color::white;
            n.font_size = 24;
            const Size s = estimate_text_size("", n.font_size, false);
            n.w = s.width;
            n.h = s.height;
            const int id = doc_.add(n);
            batch([&] {
                doc_.selection = std::vector<int>{id};
                doc_.editing_text = id;
                doc_.tool = Tool::select;
            });
            mode_ = Mode::none;
            return {.consumed = true, .redraw = true};
        }
        if (tool != Tool::select) {
            doc_.checkpoint();
            Node n;
            n.parent = parent;
            n.x = start_canvas_.x;
            n.y = start_canvas_.y;
            switch (tool) {
            case Tool::frame:
                n.kind = Kind::frame;
                n.fill = Color::white;
                break;
            case Tool::ellipse: n.kind = Kind::ellipse; break;
            case Tool::line:
                n.kind = Kind::line;
                n.has_fill = false;
                n.has_stroke = true;
                n.stroke = parent != 0 ? Color::hex(0x1E1E2E) : Color::white;
                n.stroke_width = 2;
                break;
            default: n.kind = Kind::rectangle; break;
            }
            created_ = doc_.add(n);
            doc_.selection = std::vector<int>{created_};
            mode_ = Mode::create;
            return {.consumed = true, .redraw = true};
        }

        // Select tool.
        if (const int handle = handle_at(local); handle >= 0) {
            doc_.checkpoint();
            handle_ = handle;
            begin_transform();
            mode_ = Mode::resize;
            return {.consumed = true};
        }
        const int deepest = doc_.hit(start_canvas_);
        if (event.click_count == 2 && deepest != 0) {
            // Double-click: type into text, else select the layer itself (into its group).
            const std::optional<Node> n = untrack([&] { return doc_.node(deepest); });
            batch([&] {
                doc_.selection = std::vector<int>{deepest};
                if (n && n->kind == Kind::text) {
                    doc_.editing_text = deepest;
                }
            });
            mode_ = Mode::none;
            return {.consumed = true, .redraw = true};
        }
        const int id = doc_.click_target(deepest);
        if (id == 0) {
            if (!shift_) {
                doc_.selection = std::vector<int>{};
            }
            marquee_base_ = doc_.selection.peek();
            mode_ = Mode::marquee;
            return {.consumed = true, .redraw = true};
        }
        const bool already = std::ranges::contains(doc_.selection.peek(), id);
        if (shift_) {
            doc_.selection.update([&](std::vector<int>& ids) {
                if (already) {
                    std::erase(ids, id);
                } else {
                    ids.push_back(id);
                }
            });
        } else if (!already) {
            doc_.selection = std::vector<int>{id};
        }
        begin_transform();
        moved_ = false;
        mode_ = roots_.empty() ? Mode::none : Mode::move;
        return {.consumed = true, .redraw = true};
    }

    void on_move(const PointerEvent& event, Point local) {
        const Point canvas = doc_.to_canvas(local);
        const bool shift = has(event.modifiers, KeyModifiers::shift) || shift_;
        switch (mode_) {
        case Mode::none: break;
        case Mode::pan: doc_.pan = Point{start_pan_.x + local.x - start_.x, start_pan_.y + local.y - start_.y}; break;
        case Mode::marquee: {
            overlay_.marquee = normalized(start_, local);
            const Rect area = normalized(start_canvas_, canvas);
            std::vector<int> ids = marquee_base_;
            // Layers directly on the page (and inside frames, as in Figma), not inside groups.
            for (const Node& n : doc_.nodes.peek()) {
                if (n.visible && !n.locked && intersects(area, n.bounds()) && !std::ranges::contains(ids, n.id) &&
                    doc_.click_target(n.id) == n.id && !(n.kind == Kind::frame && n.parent == 0 &&
                                                         !(area.x <= n.x && area.y <= n.y && area.max_x() >= n.x + n.w &&
                                                           area.max_y() >= n.y + n.h))) {
                    ids.push_back(n.id);
                }
            }
            // A layer and something inside it: keep the outer one.
            std::erase_if(ids, [&](int id) {
                return std::ranges::any_of(ids, [&](int other) { return other != id && doc_.is_ancestor(other, id); });
            });
            doc_.selection = ids;
            break;
        }
        case Mode::create: {
            float w = canvas.x - start_canvas_.x;
            float h = canvas.y - start_canvas_.y;
            doc_.update(created_, [&](Node& n) {
                if (n.kind == Kind::line) {
                    if (shift) { // 45-degree steps
                        const float angle = std::round(std::atan2(h, w) / (3.14159265f / 4)) * (3.14159265f / 4);
                        const float length = std::hypot(w, h);
                        w = std::cos(angle) * length;
                        h = std::sin(angle) * length;
                    }
                    n.w = w;
                    n.h = h;
                    return;
                }
                if (shift) {
                    const float side = std::max(std::abs(w), std::abs(h));
                    w = std::copysign(side, w);
                    h = std::copysign(side, h);
                }
                n.x = std::min(start_canvas_.x, start_canvas_.x + w);
                n.y = std::min(start_canvas_.y, start_canvas_.y + h);
                n.w = std::abs(w);
                n.h = std::abs(h);
            });
            break;
        }
        case Mode::move: {
            if (!moved_) {
                if (std::hypot(local.x - start_.x, local.y - start_.y) < 3) {
                    return; // a click, not a drag yet
                }
                moved_ = true;
                doc_.checkpoint();
                // Auto layout leaves dragged layers where the pointer puts them.
                doc_.floating->clear();
                doc_.floating->insert(roots_.begin(), roots_.end());
            }
            float dx = canvas.x - start_canvas_.x;
            float dy = canvas.y - start_canvas_.y;
            if (shift) { // along one axis
                (std::abs(dx) > std::abs(dy) ? dy : dx) = 0;
            }
            snap(dx, dy);
            const Rect to{base_bounds_.x + dx, base_bounds_.y + dy, base_bounds_.width, base_bounds_.height};
            batch([&] {
                doc_.set_nodes(transformed(base_, roots_, base_bounds_, to));
                doc_.drop_target = drop_target(canvas);
            });
            break;
        }
        case Mode::resize: resize(canvas, shift); break;
        }
    }

    // Where the dragged layers would go: the frame under the pointer if it isn't their parent
    // already, or their auto-layout parent (to reorder); else 0.
    int drop_target(Point canvas) const {
        const int target = doc_.frame_at(canvas, roots_);
        const std::optional<Node> first = untrack([&] { return doc_.node(roots_.front()); });
        const int parent = first ? first->parent : 0;
        if (target != parent) {
            return target;
        }
        const std::optional<Node> p = untrack([&] { return doc_.node(parent); });
        return p && p->auto_layout ? parent : 0;
    }

    void on_up(Point local) {
        if (mode_ == Mode::create) {
            const std::optional<Node> n = untrack([&] { return doc_.node(created_); });
            if (n && std::abs(n->w) < 2 && std::abs(n->h) < 2) {
                // A click: a default size, centered on it.
                doc_.update(created_, [](Node& node) {
                    if (node.kind == Kind::line) {
                        node.w = 100;
                    } else {
                        node.x -= 50;
                        node.y -= 50;
                        node.w = 100;
                        node.h = 100;
                    }
                });
            }
            doc_.tool = Tool::select;
        }
        if (mode_ == Mode::move && moved_) {
            drop(doc_.to_canvas(local));
        }
        mode_ = Mode::none;
        batch([&] {
            overlay_.marquee = std::nullopt;
            overlay_.guides = std::vector<std::pair<Point, Point>>{};
            doc_.drop_target = 0;
        });
    }

    // Finishes a move: into the frame under the pointer (or out onto the page), and into place
    // among an auto-layout frame's children.
    void drop(Point canvas) {
        doc_.floating->clear();
        const int target = doc_.frame_at(canvas, roots_);
        const std::optional<Node> first = untrack([&] { return doc_.node(roots_.front()); });
        const int parent = first ? first->parent : 0;
        const std::optional<Node> frame = untrack([&] { return doc_.node(target); });
        if (frame && frame->auto_layout) {
            // Before the first child whose middle is past the pointer.
            std::size_t index = 0;
            for (const Node& n : doc_.nodes.peek()) {
                if (n.parent != target || std::ranges::contains(roots_, n.id)) {
                    continue;
                }
                const Rect b = n.bounds();
                const float middle = frame->horizontal ? b.x + b.width / 2 : b.y + b.height / 2;
                if ((frame->horizontal ? canvas.x : canvas.y) > middle) {
                    ++index;
                }
            }
            doc_.reparent(roots_, target, index);
            return;
        }
        if (target != parent) {
            doc_.reparent(roots_, target);
            return;
        }
        doc_.relayout();
    }

    // Snaps the moved selection's edges and centers to other layers'.
    void snap(float& dx, float& dy) {
        std::vector<std::pair<Point, Point>> guides;
        if (!doc_.snapping.peek() || roots_.empty()) {
            overlay_.guides = guides;
            return;
        }
        Rect moving = base_bounds_;
        moving.x += dx;
        moving.y += dy;
        const float threshold = kSnap / doc_.zoom.peek();
        float best_x = threshold + 1;
        float best_y = threshold + 1;
        float line_x = 0;
        float line_y = 0;
        std::optional<Rect> target_x;
        std::optional<Rect> target_y;
        const float xs[] = {moving.x, moving.x + moving.width / 2, moving.max_x()};
        const float ys[] = {moving.y, moving.y + moving.height / 2, moving.max_y()};
        for (const Node& n : base_) {
            if (!n.visible || n.kind == Kind::group ||
                std::ranges::any_of(roots_, [&](int r) { return r == n.id || doc_.is_ancestor(r, n.id); })) {
                continue;
            }
            const Rect b = n.bounds();
            const float tx[] = {b.x, b.x + b.width / 2, b.max_x()};
            const float ty[] = {b.y, b.y + b.height / 2, b.max_y()};
            for (const float m : xs) {
                for (const float t : tx) {
                    if (std::abs(t - m) < std::abs(best_x)) {
                        best_x = t - m;
                        line_x = t;
                        target_x = b;
                    }
                }
            }
            for (const float m : ys) {
                for (const float t : ty) {
                    if (std::abs(t - m) < std::abs(best_y)) {
                        best_y = t - m;
                        line_y = t;
                        target_y = b;
                    }
                }
            }
        }
        if (std::abs(best_x) <= threshold && target_x) {
            dx += best_x;
            guides.push_back({{line_x, std::min(moving.y, target_x->y)}, {line_x, std::max(moving.max_y(), target_x->max_y())}});
        }
        if (std::abs(best_y) <= threshold && target_y) {
            dy += best_y;
            guides.push_back({{std::min(moving.x, target_y->x), line_y}, {std::max(moving.max_x(), target_y->max_x()), line_y}});
        }
        overlay_.guides = guides;
    }

    void resize(Point canvas, bool keep_ratio) {
        const Rect o = base_bounds_;
        float left = o.x;
        float top = o.y;
        float right = o.max_x();
        float bottom = o.max_y();
        const int h = handle_;
        if (h == 0 || h == 6 || h == 7) {
            left = canvas.x;
        }
        if (h == 2 || h == 3 || h == 4) {
            right = canvas.x;
        }
        if (h == 0 || h == 1 || h == 2) {
            top = canvas.y;
        }
        if (h == 4 || h == 5 || h == 6) {
            bottom = canvas.y;
        }
        if (keep_ratio && o.width > 0 && o.height > 0 && (h % 2 == 0)) {
            const float width = right - left;
            const float height = bottom - top;
            const float scale = std::max(std::abs(width) / o.width, std::abs(height) / o.height);
            const float w = std::copysign(o.width * scale, width);
            const float hh = std::copysign(o.height * scale, height);
            if (h == 0 || h == 6) {
                left = right - w;
            } else {
                right = left + w;
            }
            if (h == 0 || h == 2) {
                top = bottom - hh;
            } else {
                bottom = top + hh;
            }
        }
        Rect next = normalized({left, top}, {right, bottom});
        next.width = std::max(next.width, 1.f);
        next.height = std::max(next.height, 1.f);
        doc_.set_nodes(transformed(base_, roots_, o, next));
    }

    Document doc_;
    Overlay overlay_;
    Rect frame_;
    Mode mode_ = Mode::none;
    bool space_ = false;
    bool shift_ = false;
    bool moved_ = false;
    Point start_;
    Point start_canvas_;
    Point start_pan_;
    int handle_ = -1;
    int created_ = 0;
    std::vector<Node> base_;
    std::vector<int> roots_;
    Rect base_bounds_;
    std::vector<int> marquee_base_;
};

class CanvasModifier final : public EventModifier {
public:
    CanvasModifier(Document doc, Overlay overlay) : doc_(std::move(doc)), overlay_(std::move(overlay)) {}
    std::unique_ptr<EventHandler> make_handler() const override { return std::make_unique<CanvasHandler>(doc_, overlay_); }

private:
    Document doc_;
    Overlay overlay_;
};

// Reports the canvas's size (for zoom to fit and zooming about the middle).
class ViewportLayout final : public Layout {
public:
    explicit ViewportLayout(State<Size> viewport) : viewport_(std::move(viewport)) {}
    Size size_that_fits(ProposedSize proposal, std::span<const LayoutChild>) const override {
        return proposal.replacing_unspecified({800, 600});
    }
    void place_children(Rect bounds, ProposedSize, std::span<const LayoutChild> children) const override {
        for (const LayoutChild& child : children) {
            child.place({bounds.x, bounds.y}, ProposedSize{bounds.width, bounds.height});
        }
        const Size known = viewport_.peek();
        if (bounds.width != known.width || bounds.height != known.height) {
            post_to_ui_thread([viewport = viewport_, size = Size{bounds.width, bounds.height}] { viewport = size; });
        }
    }

private:
    State<Size> viewport_;
};

// --- Overlays drawn as views: text layers, frame titles, the size label, the text editor ---

View TextLayer(const Document& doc, const Node& n) {
    const int id = n.id;
    const float size = n.font_size;
    const bool bold = n.bold;
    // As a View: Text's own font() takes a fixed font, View's a reactive one.
    return View(Text(n.text.empty() ? std::string(" ") : n.text))
        .font([=] { return Font::system(std::max(size * doc.zoom(), 1.f), bold ? Weight::bold : Weight::regular); })
        .foreground_style(n.fill.opacity(n.opacity))
        .fixed_size()
        .opacity([=] { return doc.editing_text() == id || size * doc.zoom() < 2 ? 0.f : 1.f; })
        .offset([=] { return n.x * doc.zoom() + doc.pan().x; }, [=] { return n.y * doc.zoom() + doc.pan().y; })
        .allows_hit_testing(false);
}

View FrameTitle(const Document& doc, const Node& n) {
    const int id = n.id;
    return Text(n.name)
        .font(Font::system(11, Weight::medium))
        .foreground_style([=](const EnvironmentValues& env) {
            return doc.is_selected(id) ? kSelection : env.color(ThemeColor::secondary);
        })
        .fixed_size()
        .offset([=] { return n.x * doc.zoom() + doc.pan().x; }, [=] { return n.y * doc.zoom() + doc.pan().y - 18; })
        .allows_hit_testing(false);
}

View SizeLabel(const Document& doc) {
    const auto bounds = [=] { return doc.editing_text() == 0 ? doc.selection_bounds() : std::nullopt; };
    return If([=] { return bounds().has_value(); },
              Text([=] {
                  const Rect b = bounds().value_or(Rect{});
                  return std::format("{:.0f} × {:.0f}", b.width, b.height);
              })
                  .font(Font::system(11, Weight::semibold))
                  .foreground_style(Color::white)
                  .padding(EdgeInsets::symmetric(6, 2))
                  .background(kSelection, RoundedRectangle(4))
                  .fixed_size()
                  .frame({.width = 200, .alignment = Alignment::center})
                  .offset([=] {
                              const Rect b = to_screen(doc, bounds().value_or(Rect{}));
                              return b.x + b.width / 2 - 100;
                          },
                          [=] {
                              const Rect b = to_screen(doc, bounds().value_or(Rect{}));
                              return b.max_y() + 10;
                          })
                  .allows_hit_testing(false));
}

View TextEditor(const Document& doc) {
    return ForEach([=] { return std::vector<int>{doc.editing_text()}; }, [=](int id) {
        if (id == 0) {
            return View(EmptyView());
        }
        const State<bool> focused(true);
        const std::optional<Node> start = untrack([&] { return doc.node(id); });
        const float size = start ? start->font_size : 24;
        const bool bold = start && start->bold;
        const Color color = start ? start->fill : Color::white;
        const Binding<std::string> text(
            [=] {
                const std::optional<Node> n = doc.node(id);
                return n ? n->text : std::string();
            },
            [=](std::string value) {
                doc.update(id, [&](Node& n) {
                    n.text = std::move(value);
                    n.name = n.text.empty() ? std::string("Text") : n.text.substr(0, 24);
                    const Size s = estimate_text_size(n.text, n.font_size, n.bold);
                    n.w = s.width;
                    n.h = s.height;
                });
            });
        const auto finish = [=] {
            const std::optional<Node> n = untrack([&] { return doc.node(id); });
            batch([&] {
                doc.editing_text = 0;
                if (n && n->text.empty()) {
                    doc.edit([&](std::vector<Node>& all) { std::erase_if(all, [&](const Node& x) { return x.id == id; }); });
                    doc.selection = std::vector<int>{};
                }
            });
        };
        // Ends typing when it loses focus (clicking elsewhere).
        effect([=] {
            if (!focused()) {
                untrack(finish);
            }
        });
        return View(TextField("Type something", text, {.on_submit = finish, .max_lines = 20})
                        .text_field_style(PlainTextFieldStyle{})
                        .font([=] { return Font::system(std::max(size * doc.zoom(), 4.f), bold ? Weight::bold : Weight::regular); })
                        .foreground_style(color)
                        .focused(focused.binding())
                        .frame([=] {
                            const std::optional<Node> n = doc.node(id);
                            const float z = doc.zoom();
                            return Size{std::max((n ? n->w : 100.f) * z + 40, 80.f), std::max((n ? n->h : size) * z + 8, 20.f)};
                        },
                               Alignment::top_leading)
                        .offset([=] { return start ? start->x * doc.zoom() + doc.pan().x : 0.f; },
                                [=] { return start ? start->y * doc.zoom() + doc.pan().y : 0.f; })
                        .on_key_press(Key::escape, finish));
    });
}

} // namespace

View CanvasView(const Document& doc, State<Size> viewport) {
    const Overlay overlay;
    const auto text_layers = [=] {
        std::vector<Node> result;
        for (const Node& n : doc.nodes()) {
            if (n.kind == Kind::text && n.visible) {
                result.push_back(n);
            }
        }
        return result;
    };
    const auto frames = [=] {
        std::vector<Node> result;
        for (const Node& n : doc.nodes()) {
            if (n.kind == Kind::frame && n.visible && n.parent == 0) {
                result.push_back(n);
            }
        }
        return result;
    };
    return ViewportLayout(viewport)(
        ZStack({.alignment = Alignment::top_leading},
               Canvas([doc, overlay](GraphicsContext& g) { draw(g, doc, overlay); }),
               ForEach(text_layers, &Node::id, [=](const Node& n) { return TextLayer(doc, n); }),
               ForEach(frames, &Node::id, [=](const Node& n) { return FrameTitle(doc, n); }), SizeLabel(doc),
               TextEditor(doc))
            .clipped()
            .modifier(CanvasModifier(doc, overlay)));
}

} // namespace design
