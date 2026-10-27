// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_VIEW_ELEMENT_H
#define ELG_VIEW_ELEMENT_H

// The retained element tree (design.md §7): what views become when mounted. Elements hold
// layout state and paint into a display list; app code never sees them.

#include "render/display_list.h"
#include "text/text_layout.h"
#include "text/text_system.h"
#include "view/animated.h"

#include <elg/events.h>
#include <elg/graphics_context.h>
#include <elg/layout.h>
#include <elg/modifier.h>
#include <elg/path.h>
#include <elg/prop.h>
#include <elg/text.h>
#include <elg/view.h>
#include <elg/views.h>

#include <functional>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace elg::detail {

class FocusManager;

// What an element is to assistive technology (design.md §12): screen readers see a tree of these.
// An element with no role is transparent: its children join its parent's.
enum class AccessRole {
    none,
    window,
    group,
    label,
    heading,
    button,
    link,
    toggle,   // a switch
    checkbox,
    slider,
    stepper,
    progress,
    text_field,
    text_area, // multiline
    password_field,
    image,
    video,
    list,
    list_item,
    scroll_view,
    tab,
    menu,
    menu_item,
    popup_button, // a button that opens a menu (MenuButton, Picker)
    separator,    // a line between groups; not focusable
};

// A switch's or check box's state.
enum class ToggleState { off, on, mixed };

struct AccessibilityInfo {
    AccessRole role = AccessRole::none;
    std::string label;       // its name
    std::string value;       // what it holds (a text field's text, a slider's value as text)
    std::string hint;        // a longer description
    std::string placeholder;
    bool hidden = false;          // it and everything inside are left out (decorative)
    bool combine = false;         // children's text becomes its label; they aren't exposed themselves
    bool disabled = false;
    std::optional<ToggleState> toggled; // switches and check boxes
    std::optional<bool> expanded;
    bool selected = false;
    std::optional<double> numeric; // sliders, steppers, progress
    std::optional<double> minimum;
    std::optional<double> maximum;
    std::optional<double> step;
    // Actions it supports; the element performs them in accessibility_action().
    bool can_click = false;
    bool can_focus = false;
    bool can_adjust = false;     // increment / decrement
    bool can_set_value = false;  // text or numbers

    friend bool operator==(const AccessibilityInfo&, const AccessibilityInfo&) = default;
};

// What assistive technology asks an element to do.
struct AccessibilityAction {
    enum class Kind { click, focus, increment, decrement, set_value, set_numeric, scroll_into_view };
    Kind kind = Kind::click;
    std::string text;
    double number = 0;
};

// What the inspector (design.md §16) shows for an element.
struct Inspection {
    std::string name;   // e.g. "Text", "VStack", "Padding"
    std::string detail; // a one-line summary, e.g. the text
    std::vector<std::pair<std::string, std::string>> properties;
};

// While alive, layout arranges every element, even ones nothing changed for (frames of windows
// with animations running, whose elements move without being invalidated).
class FullLayoutScope {
public:
    explicit FullLayoutScope(bool full) noexcept;
    ~FullLayoutScope();
    FullLayoutScope(const FullLayoutScope&) = delete;
    FullLayoutScope& operator=(const FullLayoutScope&) = delete;

private:
    bool previous_;
};

class Element {
public:
    virtual ~Element();

    // Describes this element for the inspector. The default names it after its type.
    virtual void inspect(Inspection& out) const;
    // Describes this element to assistive technology (design.md §12). The default: no role
    // (transparent).
    virtual void accessibility(AccessibilityInfo&) const {}
    // Carries out an action from assistive technology; false if it can't.
    virtual bool accessibility_action(const AccessibilityAction&) { return false; }
    // A stable id for the accessibility tree, unique for the process.
    std::uint64_t accessibility_id() const noexcept;
    // The proposal it was last placed with (inspector); nullopt before its first layout.
    std::optional<ProposedSize> last_proposal() const noexcept { return last_proposal_; }

    // Memoized per proposal until invalidate_layout().
    Size size_that_fits(ProposedSize proposal);
    // Sizes the element for `proposal`, positions it, and lays out its children. A changed frame
    // eases to its new place during an animated update (design.md §9.5).
    void place(Point position, ProposedSize proposal);
    // Post-layout, even with cached arrangement or culled painting (DPI can change independently).
    // Observers must queue work, not change the retained tree during this traversal.
    void notify_render_size(float scale);
    void paint(DisplayList& list) const {
        if (frame_.animating()) {
            list.animating = true;
        }
        // Out of sight (scrolled away): skipped, unless it may be on its way in.
        if (list.cull && paint_bounds_ && cullable() && !overlaps(*paint_bounds_, *list.cull)) {
            list.animating |= moving_;
            return;
        }
        paint_impl(list);
        if (focus_manager_) {
            paint_focus_ring(list);
        }
    }

    // Where the element is drawn now: its laid-out frame, or on the way there.
    Rect frame() const { return frame_.current(); }
    // frame() moved by the offsets of the elements around it (.offset draws, and hits, content
    // away from its layout): where it is on screen, e.g. for anchoring popups.
    Rect drawn_frame() const {
        Rect result = frame();
        for (const Element* element = parent_; element; element = element->parent_) {
            const Point offset = element->content_offset();
            result.x += offset.x;
            result.y += offset.y;
        }
        return result;
    }
    // How far the content is drawn from where it's laid out (.offset).
    virtual Point content_offset() const { return {}; }

    // The `.animation(...)` this element is inside, if any.
    void set_implicit_animation(std::shared_ptr<ImplicitAnimation> animation) noexcept {
        implicit_ = std::move(animation);
    }
    // `.animates_layout(false)`: frame changes always snap.
    void set_animates_layout(bool enabled) noexcept { animates_layout_ = enabled; }
    // The animation for a change applied now: the running transaction's, else an implicit one
    // that fired in this update.
    const Animation* change_animation() const noexcept;

    // Painting may skip this element when it's entirely outside what can be seen. false: never
    // (it can draw anywhere: canvases, transitions under way), nor its ancestors.
    virtual bool cullable() const { return true; }

    virtual float layout_priority() const noexcept { return 0; }
    virtual float z_index() const noexcept { return 0; }
    virtual bool is_spacer() const noexcept { return false; }

    // Hit testing (design.md §10): true if `point` (window points) hits this element's content.
    // On a hit, `path` holds the elements from this one down to the innermost hit element.
    bool hit_test(Point point, std::vector<Element*>& path);
    // The handler for input to this element, if it has one.
    virtual EventHandler* event_handler() noexcept { return nullptr; }
    // For elements with a handler: how long hover takes to start and end (the handler's own
    // delay, else the environment's).
    virtual HoverDelay effective_hover_delay() const { return {}; }
    // Scroll views: scrolls so `rect` (window points, as currently drawn) comes into sight.
    virtual void reveal(Rect) {}
    // Scroll views: the part of their content that can be seen (window points), for lazy stacks.
    virtual std::optional<Rect> visible_rect() const { return std::nullopt; }
    // Scroll containers: content inside moved by `delta` along `axis` (rows above the visible
    // part changed size); move the scroll offset with it so what's on screen stays put. Returns
    // false if it didn't (not a scroll container, or it's following the end).
    virtual bool anchor_shift(Axis, float) { return false; }
    // Lazy stacks: where the item with this key is (or would be), along the stack's axis from its
    // start; estimated for items not built.
    virtual std::optional<float> item_offset(const ItemKey&) const { return std::nullopt; }
    // Non-null for TransitionElement (If/ForEach use a child's own transition when it has one).
    virtual class TransitionElement* as_transition() noexcept { return nullptr; }

    // The element that owns this one (nullptr for the root). Keys bubble along it.
    Element* parent() const noexcept { return parent_; }
    // For focusable elements: the window's focus (the element draws the ring while it has visible
    // focus, and lets go of focus when destroyed) and the ring's color (empty: no ring).
    void set_focus_manager(FocusManager* focus, std::function<Color()> ring_color) {
        focus_manager_ = focus;
        focus_ring_color_ = std::move(ring_color);
    }

    // Something that affects this element's size changed (e.g. reactive text): drops the cached
    // sizes of this element and every ancestor.
    void invalidate_layout() noexcept;
    // Where it draws changed without its size changing (an offset): the next layout visits it
    // and its ancestors again to recompute paint bounds.
    void invalidate_paint_bounds() noexcept {
        for (Element* element = this; element; element = element->parent_) {
            element->needs_arrange_ = true;
        }
    }
    // A value from View::layout_value on this element or the modifiers wrapping its content.
    virtual const void* find_layout_value(const std::type_info&) const noexcept { return nullptr; }

    // How a parent layout sees this element: itself, or for If/ForEach the elements they show
    // right now (flattened), or nothing for EmptyView.
    virtual void collect_layout_children(std::vector<Element*>& out) { out.push_back(this); }
    // True when a parent layout places this element's layout children (collect_layout_children)
    // instead of the element: it's never placed itself inside a layout, and its paint bounds are
    // its children's.
    virtual bool flattened_by_layouts() const noexcept { return false; }
    // Calls `visit` for each child this element owns.
    virtual void for_each_child(const std::function<void(Element&)>&) {}
    // Removed If/ForEach children still animating out: painted by the parent layout, not laid out.
    virtual void collect_exiting(std::vector<Element*>&) {}
    // Destroys removed children whose exit animation has finished.
    virtual void prune_exiting() {}

protected:
    // The next frame change snaps instead of animating (e.g. text whose content just changed:
    // the glyphs can't morph, so their position shouldn't glide either).
    void snap_next_layout() noexcept { snap_next_layout_ = true; }

public:

protected:
    // Records `child` as owned by this element (for layout invalidation).
    void adopt(Element& child) noexcept { child.parent_ = this; }
    // Removed children animating out aren't laid out, so they'd stay where they were removed. When
    // their container moves (scrolling, its parent's layout), they move with it: call from arrange
    // with the exiting children it paints.
    void move_exiting(const std::vector<Element*>& exiting);
    // Tells the ancestors that the set of children a layout sees has changed.
    void notify_structure_changed() {
        if (parent_) {
            parent_->on_structure_changed();
        }
    }
    // A descendant If/ForEach changed what it shows. Layouts rebuild their child lists; other
    // elements pass it on.
    virtual void on_structure_changed() { notify_structure_changed(); }
    // This element's cached sizes were dropped (it, or something inside, changed).
    virtual void on_layout_invalidated() noexcept {}
    virtual void on_render_size(float) {}

    // Hits nothing by default (spacers, empty views). Containers test their children.
    virtual bool hit_impl(Point, std::vector<Element*>&) { return false; }

    virtual Size measure(ProposedSize proposal) = 0;
    // `bounds` is the element's frame.
    virtual void arrange(Rect bounds, ProposedSize proposal) = 0;
    virtual void paint_impl(DisplayList& list) const = 0;
    // Its arrangement depends on more than its frame, proposal, and invalidate_layout() (a scroll
    // offset, the visible part of a scroll view, a caret): it's arranged every layout.
    virtual bool always_arranges() const { return false; }
    // Where it draws, given its frame's and children's (strokes, shadows, offsets reach further).
    virtual Rect paint_extent(Rect bounds) const { return bounds; }
    // Its drawing is moving (an offset animating), so it may come into sight while skipped.
    virtual bool drawing_moves() const { return false; }

private:
    void paint_focus_ring(DisplayList& list) const;
    // After layout: paint_bounds_ and moving_ for this element and everything inside.
    void update_paint_bounds();
    static bool overlaps(const Rect& a, const Rect& b) noexcept {
        return a.x < b.max_x() && b.x < a.max_x() && a.y < b.max_y() && b.y < a.max_y();
    }

    Element* parent_ = nullptr;
    FocusManager* focus_manager_ = nullptr;
    std::function<Color()> focus_ring_color_;
    Animated<Rect> frame_{Rect{}};
    bool placed_ = false;
    bool animates_layout_ = true;
    bool snap_next_layout_ = false;
    std::shared_ptr<ImplicitAnimation> implicit_;
    std::vector<std::pair<ProposedSize, Size>> size_cache_;
    std::optional<ProposedSize> last_proposal_;
    std::optional<Rect> paint_bounds_; // as of the last layout; nullopt: unknown or unbounded
    bool moving_ = false;              // something inside is moving (frames, offsets, transitions)
    bool needs_arrange_ = true;        // it or something inside was invalidated since its arrange
    Point moved_by_;                   // how far the last place() moved it (for exiting children)
    bool always_arranges_inside_ = false; // always_arranges(), here or anywhere inside
    mutable std::uint64_t accessibility_id_ = 0;
};

class EmptyElement final : public Element {
public:
    // Takes no place (and no spacing) in a parent layout.
    void collect_layout_children(std::vector<Element*>&) override {}
    bool flattened_by_layouts() const noexcept override { return true; }

protected:
    Size measure(ProposedSize) override { return {}; }
    void arrange(Rect, ProposedSize) override {}
    void paint_impl(DisplayList&) const override {}
};

enum class ShapeKind { rectangle, rounded_rectangle, capsule, circle, path };

// A path shape's geometry: a fixed path (optionally in its own view box), or one built per frame.
struct ShapePath {
    Path path;
    std::optional<Rect> view_box;
    std::function<Path(Rect)> builder;
    FillRule rule = FillRule::non_zero;
};

struct ShapeStroke {
    Prop<Color> color;
    float width = 1;
    bool centered = true; // false: inside the edge
    StrokeStyle style;    // caps, joins, dashes (style.width == width)
};

// A Shape view's description (not to be confused with the public elg::ShapeStyle, its fill).
struct ShapeSpec {
    ShapeKind kind = ShapeKind::rectangle;
    CornerRadii radii{}; // rounded_rectangle only
    std::optional<Prop<ShapeStyle>> fill{};
    std::optional<ShapeStroke> stroke{};
    std::shared_ptr<const ShapePath> path{}; // ShapeKind::path
};

// The rounded rect a shape occupies inside `frame`.
struct ShapeOutline {
    Rect rect;
    CornerRadii radii;
};
ShapeOutline shape_outline(const ShapeSpec& spec, Rect frame);

// A shape's filled area as flattened polygons, in points from the frame's corner (for a frame of
// `size`, flattened finely enough for `scale` pixels per point).
struct ShapeFill {
    std::shared_ptr<const std::vector<std::vector<Point>>> polygons;
    Rect bounds;
    FillRule rule = FillRule::non_zero;
};
ShapeFill shape_fill(const ShapeSpec& spec, Size size, float scale);
bool shape_fill_contains(const ShapeFill& fill, Point local);
// Whether `point` is inside a rounded rect.
bool outline_contains(const ShapeOutline& outline, Point point);

// The points a shape covers in a frame, for hit testing: analytic for the rounded-rect kinds,
// the flattened path with its fill rule for paths (cached per size and scale). A filled shape,
// clip_shape and content_shape all hit test through one, so the same shape takes the same
// points wherever it's used.
class ShapeHitArea {
public:
    bool contains(const ShapeSpec& spec, Point point, Rect frame) const;
    // A path's filled area for a frame of `size`, flattened for `scale` pixels per point.
    const ShapeFill& fill(const ShapeSpec& spec, Size size, float scale) const;

private:
    mutable const ShapeSpec* spec_ = nullptr;
    mutable ShapeFill fill_;
    mutable Size size_{-1, -1};
    mutable float scale_ = 0;
};

class ShapeElement final : public Element {
public:
    void inspect(Inspection& out) const override;
    explicit ShapeElement(ShapeSpec spec) : spec_(std::move(spec)) {}
    Rect paint_extent(Rect bounds) const override {
        return spec_.stroke ? bounds.inset(-spec_.stroke->width) : bounds;
    }

    // The resolved fill: the shape's own (static or reactive), or for shapes with neither a fill
    // nor a stroke, the environment's foreground. Animates during animated updates.
    void set_fill(ShapeStyle fill, const Animation* animation) {
        has_fill_ = true;
        fill_.set(std::move(fill), animation);
    }
    void set_stroke_color(Color color, const Animation* animation) { stroke_color_.set(color, animation); }

protected:
    Size measure(ProposedSize proposal) override;
    void arrange(Rect, ProposedSize) override {}
    void paint_impl(DisplayList& list) const override;
    bool hit_impl(Point point, std::vector<Element*>& path) override;

private:
    using Polygons = std::vector<std::vector<Point>>;
    // Flattened geometry for a frame size and display scale, in points from the frame's corner.
    struct Geometry {
        Size size{-1, -1};
        float scale = 0;
        std::shared_ptr<const Polygons> fill;
        Rect fill_bounds;
        std::shared_ptr<const Polygons> stroke;
        Rect stroke_bounds;
    };

    // Whether this shape draws through the path pipeline (paths, and dashed strokes of any shape).
    bool uses_path() const noexcept;
    bool hit_stroke_path(Point point) const;
    const Geometry& geometry(Size size, float scale) const;

    ShapeSpec spec_;
    bool has_fill_ = false;
    Animated<ShapeStyle> fill_{ShapeStyle(Color::clear)};
    Animated<Color> stroke_color_{Color::clear};
    mutable Geometry geometry_;
    ShapeHitArea hit_area_;
};

struct TextStyle {
    std::optional<Font> font;
    std::optional<Weight> weight;
    std::optional<Prop<Color>> color;
    int line_limit = 0; // 0: unlimited
    TextAlignment alignment = TextAlignment::leading;
};

class TextElement final : public Element {
public:
    void accessibility(AccessibilityInfo& out) const override;
    void inspect(Inspection& out) const override;
    // Font, content, and color are set with the setters (from static values or reactive
    // arguments), font first.
    TextElement(TextSystem& text, int line_limit, TextAlignment alignment);

    // Loads the font (throws elg::ResourceError if it can't), reshapes, and invalidates layout.
    void set_font(const Font& font);
    // Reshapes the text and invalidates layout.
    void set_content(std::string_view content);
    void set_color(Color color, const Animation* animation) { color_.set(color, animation); }

protected:
    Size measure(ProposedSize proposal) override;
    void arrange(Rect bounds, ProposedSize proposal) override;
    void paint_impl(DisplayList& list) const override;
    bool hit_impl(Point point, std::vector<Element*>&) override;
    void on_layout_invalidated() noexcept override { arranged_for_.reset(); }

private:
    TextLayoutResult layout(ProposedSize proposal) const;
    // Recomputes the lines for the new text at the last proposal (see text.cpp).
    void relayout_now();

    TextSystem* text_;
    const FontFace* face_ = nullptr;
    float size_ = 15;
    Animated<Color> color_{Color::black};
    int line_limit_;
    TextAlignment alignment_;
    float ascent_ = 0;
    float line_height_ = 0;
    std::string content_;
    std::vector<ShapedParagraph> paragraphs_;
    ShapedParagraph ellipsis_;
    float ellipsis_width_ = 0;
    TextLayoutResult arranged_;
    std::optional<ProposedSize> arranged_for_; // the proposal arranged_ is for, until text or font change
};

class SpacerElement final : public Element {
public:
    SpacerElement(float min_length, std::optional<Axis> axis) : min_length_(min_length), axis_(axis) {}

    bool is_spacer() const noexcept override { return true; }

protected:
    Size measure(ProposedSize proposal) override;
    void arrange(Rect, ProposedSize) override {}
    void paint_impl(DisplayList&) const override {}

private:
    float min_length_;
    std::optional<Axis> axis_;
};

class CanvasElement final : public Element {
public:
    explicit CanvasElement(std::function<void(GraphicsContext&)> draw) : draw_(std::move(draw)) {}
    bool cullable() const override { return false; } // it may draw outside its frame

protected:
    Size measure(ProposedSize proposal) override;
    void arrange(Rect, ProposedSize) override {}
    void paint_impl(DisplayList& list) const override;
    bool hit_impl(Point point, std::vector<Element*>&) override;

private:
    std::function<void(GraphicsContext&)> draw_;
};

// `.transition(...)`, and the wrapper If/ForEach put around each child: animates the child in
// when added and out when removed during an animated update.
class TransitionElement final : public Element {
public:
    const void* find_layout_value(const std::type_info& key) const noexcept override {
        return content_->find_layout_value(key);
    }
    TransitionElement(Transition transition, std::unique_ptr<Element> content)
        : transition_(std::move(transition)), content_(std::move(content)) {
        adopt(*content_);
    }

    void insert(const Animation* animation);
    // Starts the exit; returns false if there's nothing to animate (remove it right away).
    bool remove(const Animation* animation);
    bool exit_finished() const { return removing_ && !progress_.animating(); }
    // Moved, scaled, or on its way out, it draws away from its frame.
    bool cullable() const override { return !removing_ && progress_.current() >= 1; }
    bool drawing_moves() const override { return progress_.animating(); }

    TransitionElement* as_transition() noexcept override { return this; }
    // An If/ForEach item that is itself an If/ForEach (a GridRow in a ForEach, say): its parent
    // layout places what the inner one shows, as if the two were one fragment.
    void collect_layout_children(std::vector<Element*>& out) override {
        if (content_->flattened_by_layouts()) {
            content_->collect_layout_children(out);
        } else {
            out.push_back(this);
        }
    }
    bool flattened_by_layouts() const noexcept override { return content_->flattened_by_layouts(); }
    void collect_exiting(std::vector<Element*>& out) override { content_->collect_exiting(out); }
    void prune_exiting() override { content_->prune_exiting(); }
    float layout_priority() const noexcept override { return content_->layout_priority(); }
    float z_index() const noexcept override { return content_->z_index(); }
    bool is_spacer() const noexcept override { return content_->is_spacer(); }
    void for_each_child(const std::function<void(Element&)>& visit) override { visit(*content_); }

protected:
    Size measure(ProposedSize proposal) override { return content_->size_that_fits(proposal); }
    void arrange(Rect bounds, ProposedSize proposal) override { content_->place({bounds.x, bounds.y}, proposal); }
    void paint_impl(DisplayList& list) const override;
    bool hit_impl(Point point, std::vector<Element*>& path) override;

private:
    Transition transition_;
    std::unique_ptr<Element> content_;
    Animated<float> progress_{1.f}; // 0: out, 1: in place
    bool removing_ = false;
};

class LayoutElement final : public Element {
public:
    void inspect(Inspection& out) const override;
    LayoutElement(std::shared_ptr<const Layout> layout, std::vector<std::unique_ptr<Element>> children);

    void for_each_child(const std::function<void(Element&)>& visit) override {
        for (const auto& child : children_) {
            visit(*child);
        }
    }

protected:
    Size measure(ProposedSize proposal) override;
    void arrange(Rect bounds, ProposedSize proposal) override;
    void paint_impl(DisplayList& list) const override;
    bool hit_impl(Point point, std::vector<Element*>& path) override;
    void on_structure_changed() override;
    void on_layout_invalidated() noexcept override { cache_.stale = true; }

private:
    void rebuild_children();
    std::vector<Element*> paint_order() const;

    std::shared_ptr<const Layout> layout_;
    LayoutCache cache_; // for CachedLayout
    std::vector<std::unique_ptr<Element>> children_; // owned, as built
    std::vector<Element*> laid_out_;                 // what the layout sees: If/ForEach flattened
    std::vector<LayoutChild> handles_;
};

class LayoutModifierElement final : public Element {
public:
    const void* find_layout_value(const std::type_info& key) const noexcept override;
    void inspect(Inspection& out) const override;
    LayoutModifierElement(std::shared_ptr<const LayoutModifier> modifier, std::unique_ptr<Element> content)
        : modifier_(std::move(modifier)), content_(std::move(content)) {
        adopt(*content_);
    }

    float layout_priority() const noexcept override;
    float z_index() const noexcept override;
    bool is_spacer() const noexcept override { return content_->is_spacer(); }
    void for_each_child(const std::function<void(Element&)>& visit) override { visit(*content_); }

protected:
    Size measure(ProposedSize proposal) override;
    void arrange(Rect bounds, ProposedSize proposal) override;
    void paint_impl(DisplayList& list) const override { content_->paint(list); }
    bool hit_impl(Point point, std::vector<Element*>& path) override { return content_->hit_test(point, path); }

private:
    std::shared_ptr<const LayoutModifier> modifier_;
    std::unique_ptr<Element> content_;
};

// Clip, opacity, and shadow of its content (design.md §9.4 cost model): a single shape takes the
// fast path (alpha multiply, analytic shadow); anything else is rendered as an offscreen layer.
class EffectElement final : public Element {
public:
    const void* find_layout_value(const std::type_info& key) const noexcept override {
        return content_->find_layout_value(key);
    }
    void inspect(Inspection& out) const override;
    EffectElement(Effects effects, std::unique_ptr<Element> content)
        : effects_(std::move(effects)), content_(std::move(content)) {
        adopt(*content_);
    }

    void set_opacity(float opacity, const Animation* animation) { opacity_.set(opacity, animation); }
    void set_offset_x(float x, const Animation* animation) {
        offset_x_.set(x, animation);
        invalidate_paint_bounds();
    }
    void set_offset_y(float y, const Animation* animation) {
        offset_y_.set(y, animation);
        invalidate_paint_bounds();
    }
    void set_z_index(float z) noexcept { z_index_ = z; }
    void set_blur(float radius, const Animation* animation) {
        blur_.set(radius, animation);
        invalidate_paint_bounds();
    }
    void set_filter_amount(float amount, const Animation* animation) { filter_amount_.set(amount, animation); }
    void set_filter_color(Color color, const Animation* animation) { filter_color_.set(color, animation); }
    void set_rotation(float degrees, const Animation* animation) {
        rotation_.set(degrees, animation);
        invalidate_paint_bounds();
    }
    void set_scale_x(float scale, const Animation* animation) {
        scale_x_.set(scale, animation);
        invalidate_paint_bounds();
    }
    void set_scale_y(float scale, const Animation* animation) {
        scale_y_.set(scale, animation);
        invalidate_paint_bounds();
    }
    void for_each_child(const std::function<void(Element&)>& visit) override { visit(*content_); }
    Rect paint_extent(Rect bounds) const override;
    bool drawing_moves() const override {
        return offset_x_.animating() || offset_y_.animating() || rotation_.animating() || scale_x_.animating() ||
               scale_y_.animating() || blur_.animating();
    }
    Point content_offset() const override { return {offset_x_.current(), offset_y_.current()}; }

    float layout_priority() const noexcept override { return content_->layout_priority(); }
    float z_index() const noexcept override { return z_index_.value_or(content_->z_index()); }
    bool is_spacer() const noexcept override { return content_->is_spacer(); }

protected:
    Size measure(ProposedSize proposal) override { return content_->size_that_fits(proposal); }
    void arrange(Rect bounds, ProposedSize proposal) override { content_->place({bounds.x, bounds.y}, proposal); }
    void paint_impl(DisplayList& list) const override;
    bool hit_impl(Point point, std::vector<Element*>& path) override;
    void on_render_size(float scale) override;

private:
    std::optional<PixelSize> last_render_size_;
    // The clip shape's fill when it's a path (it clips through a mask), cached per size and scale;
    // nullptr for the rounded-rect shapes, which clip analytically.
    const ShapeFill* clip_path(float scale) const;
    std::optional<LayerTransform> layer_transform() const;

    Effects effects_;
    std::unique_ptr<Element> content_;
    ShapeHitArea clip_area_;
    ShapeHitArea content_area_;
    Animated<float> opacity_{1.f}; // resolved from effects_.opacity
    Animated<float> offset_x_{0.f};
    Animated<float> offset_y_{0.f};
    Animated<float> blur_{0.f};
    Animated<float> filter_amount_{1.f};
    Animated<Color> filter_color_{Color::white};
    Animated<float> rotation_{0.f};
    Animated<float> scale_x_{1.f};
    Animated<float> scale_y_{1.f};
    std::optional<float> z_index_; // .z_index(...)
};

class DecorationElement final : public Element {
public:
    const void* find_layout_value(const std::type_info& key) const noexcept override {
        return content_->find_layout_value(key);
    }
    void inspect(Inspection& out) const override;
    DecorationElement(std::shared_ptr<const DecorationModifier> modifier, std::unique_ptr<Element> content,
                      std::unique_ptr<Element> decoration)
        : modifier_(std::move(modifier)), content_(std::move(content)), decoration_(std::move(decoration)) {
        adopt(*content_);
        adopt(*decoration_);
    }

    float layout_priority() const noexcept override { return content_->layout_priority(); }
    float z_index() const noexcept override { return content_->z_index(); }
    bool is_spacer() const noexcept override { return content_->is_spacer(); }
    void for_each_child(const std::function<void(Element&)>& visit) override {
        visit(*content_);
        visit(*decoration_);
    }

protected:
    Size measure(ProposedSize proposal) override { return content_->size_that_fits(proposal); }
    void arrange(Rect bounds, ProposedSize proposal) override;
    void paint_impl(DisplayList& list) const override;
    bool hit_impl(Point point, std::vector<Element*>& path) override;

private:
    std::shared_ptr<const DecorationModifier> modifier_;
    std::unique_ptr<Element> content_;
    std::unique_ptr<Element> decoration_;
};

// A view with an EventModifier: owns the handler; layout, paint, and hit testing pass through.
class EventElement final : public Element {
public:
    const void* find_layout_value(const std::type_info& key) const noexcept override {
        return content_->find_layout_value(key);
    }
    // `environment_delay` reads the environment's hover delay (used unless the handler has its own).
    EventElement(std::unique_ptr<EventHandler> handler, std::unique_ptr<Element> content,
                 std::function<HoverDelay()> environment_delay)
        : handler_(std::move(handler)), content_(std::move(content)), environment_delay_(std::move(environment_delay)) {
        adopt(*content_);
    }

    HoverDelay effective_hover_delay() const override {
        return handler_->hover_delay().value_or(environment_delay_());
    }

    float layout_priority() const noexcept override { return content_->layout_priority(); }
    float z_index() const noexcept override { return content_->z_index(); }
    bool is_spacer() const noexcept override { return content_->is_spacer(); }
    EventHandler* event_handler() noexcept override { return handler_.get(); }
    void for_each_child(const std::function<void(Element&)>& visit) override { visit(*content_); }

protected:
    Size measure(ProposedSize proposal) override { return content_->size_that_fits(proposal); }
    void arrange(Rect bounds, ProposedSize proposal) override { content_->place({bounds.x, bounds.y}, proposal); }
    void paint_impl(DisplayList& list) const override { content_->paint(list); }
    bool hit_impl(Point point, std::vector<Element*>& path) override {
        if (content_->hit_test(point, path)) {
            return true;
        }
        const Rect bounds = frame();
        return handler_->covers_frame() && point.x >= bounds.x && point.x < bounds.max_x() && point.y >= bounds.y &&
               point.y < bounds.max_y();
    }

private:
    std::unique_ptr<EventHandler> handler_;
    std::unique_ptr<Element> content_;
    std::function<HoverDelay()> environment_delay_;
};

// A Button: behavior here, appearance from its ButtonStyle's body (the content). Hover and press
// are written to States the style reads, so only what the style derives from them updates.
class ButtonElement final : public Element, private EventHandler {
public:
    void accessibility(AccessibilityInfo& out) const override;
    bool accessibility_action(const AccessibilityAction& action) override;
    ButtonElement(std::unique_ptr<Element> content, std::function<void()> action, State<bool> pressed,
                  State<bool> hovered, std::function<bool()> enabled, std::function<HoverDelay()> hover_delay)
        : content_(std::move(content)), action_(std::move(action)), pressed_(std::move(pressed)),
          hovered_(std::move(hovered)), enabled_(std::move(enabled)), hover_delay_(std::move(hover_delay)) {
        adopt(*content_);
    }

    HoverDelay effective_hover_delay() const override { return hover_delay_(); }

    EventHandler* event_handler() noexcept override { return this; }
    void for_each_child(const std::function<void(Element&)>& visit) override { visit(*content_); }

    // Written on focus changes, for styles (ButtonConfiguration::is_focused).
    void set_focus_state(State<bool> focused) { focused_ = std::move(focused); }
    void set_action(std::function<void()> action) { action_ = std::move(action); }
    // Called after the hover delay when the pointer enters (true) or leaves (false).
    void set_on_hover(std::function<void(bool)> on_hover) { on_hover_ = std::move(on_hover); }
    // Also activate on these keys (e.g. Right for a submenu row).
    void set_extra_key(Key key) noexcept { extra_key_ = key; }

protected:
    Size measure(ProposedSize proposal) override { return content_->size_that_fits(proposal); }
    void arrange(Rect bounds, ProposedSize proposal) override { content_->place({bounds.x, bounds.y}, proposal); }
    void paint_impl(DisplayList& list) const override { content_->paint(list); }
    bool hit_impl(Point point, std::vector<Element*>&) override;

private:
    EventResult pointer(const PointerEvent& event, Rect frame) override;
    EventResult hover(bool inside) override;
    EventResult key(const KeyEvent& event) override;
    bool focusable() const override;
    EventResult focus(bool focused) override;

    std::unique_ptr<Element> content_;
    std::function<void()> action_;
    State<bool> pressed_;
    State<bool> hovered_;
    std::function<bool()> enabled_;
    std::function<HoverDelay()> hover_delay_;
    std::optional<State<bool>> focused_;
    std::function<void(bool)> on_hover_;
    Key extra_key_ = Key::unknown;
    bool down_ = false;       // the pointer went down on the button and hasn't come up
    bool space_down_ = false; // Space went down while focused; the action runs when it comes up
};

// What a ScrollPosition drives: the scroll view it's attached to, or a request waiting for one.
struct ScrollRequest {
    enum class Kind { top, end, item };
    Kind kind = Kind::top;
    bool animated = true;
    ItemKey key;
    int settle = 0; // item: layouts left to re-aim while estimated sizes become real
};

struct ScrollTarget {
    class ScrollElement* element = nullptr;
    std::optional<ScrollRequest> request;
    bool* needs_frame = nullptr;
};

struct ScrollTargetAccess {
    static const std::shared_ptr<ScrollTarget>& target(const ScrollPosition& position) { return position.target_; }
};

// ScrollView (scroll.cpp).
class ScrollElement final : public Element, private EventHandler {
public:
    void accessibility(AccessibilityInfo& out) const override;
    bool accessibility_action(const AccessibilityAction& action) override;
    ScrollElement(ScrollViewOptions options, std::unique_ptr<Element> content, std::function<Color()> indicator_color,
                  bool* needs_frame)
        : options_(std::move(options)), content_(std::move(content)), indicator_color_(std::move(indicator_color)) {
        adopt(*content_);
        if (options_.position) {
            const auto& target = ScrollTargetAccess::target(*options_.position);
            target->element = this;
            target->needs_frame = needs_frame;
        }
    }
    ~ScrollElement() override {
        if (options_.position) {
            if (const auto& target = ScrollTargetAccess::target(*options_.position); target->element == this) {
                target->element = nullptr;
            }
        }
    }

    // ScrollViewOptions::shows_indicators and ::sticks_to_end.
    void set_shows_indicators(bool shows) { shows_indicators_ = shows; }
    void set_sticks_to_end(bool sticks) {
        if (std::exchange(sticks_to_end_, sticks) != sticks) {
            invalidate_layout();
        }
    }

    EventHandler* event_handler() noexcept override { return this; }
    void for_each_child(const std::function<void(Element&)>& visit) override { visit(*content_); }
    void reveal(Rect rect) override;
    std::optional<Rect> visible_rect() const override { return viewport_; }
    bool always_arranges() const override { return true; } // the offset places the content
    bool anchor_shift(Axis axis, float delta) override;

protected:
    Size measure(ProposedSize proposal) override;
    void arrange(Rect bounds, ProposedSize proposal) override;
    void paint_impl(DisplayList& list) const override;
    bool hit_impl(Point point, std::vector<Element*>& path) override;

private:
    struct Thumb {
        Rect rect;    // window points
        float travel; // how far the thumb moves from start to end
    };

    EventResult pointer(const PointerEvent& event, Rect frame) override;
    EventResult hover(bool inside) override;
    EventResult scroll(const ScrollEvent& event) override;
    EventResult key(const KeyEvent& event) override;
    // Mouse wheels, and trackpads whose gestures the platform doesn't follow.
    EventResult wheel(const ScrollEvent& event);
    // Trackpad gestures: the content follows the fingers, stretches past the edges, and coasts.
    EventResult gesture(const ScrollEvent& event);

    // A critically damped spring back to an edge: after letting go past it, or bouncing off it.
    struct Rebound {
        double start = 0;       // frame_time()
        float edge = 0;
        float displacement = 0; // from the edge at the start
        float velocity = 0;     // points per second, at the start
        bool active = false;

        float at(double time) const;
        bool settled(double time) const;
    };
    // Gliding on after the fingers lift, slowing down exponentially.
    struct Coast {
        double start = 0; // frame_time()
        float from = 0;
        float velocity = 0; // points per second, at the start
        bool active = false;

        float at(double time) const;
        float velocity_at(double time) const;
        float end() const;
    };

    bool scrolls(Axis axis) const noexcept;
    ProposedSize content_proposal(ProposedSize proposal) const noexcept;
    Point max_offset() const noexcept;
    float offset(Axis axis) const;
    Point current_offset() const { return {offset(Axis::horizontal), offset(Axis::vertical)}; }
    // Whether a gesture, a coast or a rebound moves the offset (maybe past the edges).
    bool stretching() const noexcept {
        return touching_ || coast_x_.active || coast_y_.active || rebound_x_.active || rebound_y_.active;
    }
    void stop_motion();
    float finger_velocity(double now) const;
    // Scrolls to `target` (clamped to the content); false if that's where it already is.
    bool scroll_to(Point target, const Animation* animation);
    std::optional<Thumb> thumb(Axis axis) const;
    std::optional<Axis> thumb_at(Point point) const;
    void show_indicators();
    void fade_indicators();
    void apply_request();

    ScrollViewOptions options_;
    bool shows_indicators_ = true;
    bool sticks_to_end_ = false;
    std::unique_ptr<Element> content_;
    std::function<Color()> indicator_color_;
    Rect viewport_;     // from the last arrange
    Size content_size_; // the content's ideal size there
    Point target_;      // where the offset is heading
    Animated<float> offset_x_{0.f};
    Animated<float> offset_y_{0.f};
    Animated<float> indicators_{0.f}; // scroll bar opacity
    // Trackpad gestures.
    std::optional<Axis> lock_;   // the axis the gesture follows: the one it started along
    bool touching_ = false;      // the fingers are scrolling this view
    Point stretch_;              // where the fingers have taken the offset, before resistance
    // The fingers' recent movements along lock_, for their speed when they lift.
    struct Move {
        double time; // ScrollEvent::time
        float delta;
    };
    std::vector<Move> moves_;
    Coast coast_x_;
    Coast coast_y_;
    Rebound rebound_x_;
    Rebound rebound_y_;
    bool pinned_ = true; // at the end (sticks_to_end follows it); the first layout counts
    bool hovered_ = false;
    std::optional<Axis> dragging_; // a scroll bar thumb being dragged
    Point drag_start_;
    Point drag_offset_;
};

} // namespace elg::detail

#endif // ELG_VIEW_ELEMENT_H
