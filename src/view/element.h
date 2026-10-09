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

class Element {
public:
    virtual ~Element();

    // Memoized per proposal until invalidate_layout().
    Size size_that_fits(ProposedSize proposal);
    // Sizes the element for `proposal`, positions it, and lays out its children. A changed frame
    // eases to its new place during an animated update (design.md §9.5).
    void place(Point position, ProposedSize proposal);
    void paint(DisplayList& list) const {
        if (frame_.animating()) {
            list.animating = true;
        }
        paint_impl(list);
        if (focus_manager_) {
            paint_focus_ring(list);
        }
    }

    // Where the element is drawn now: its laid-out frame, or on the way there.
    Rect frame() const { return frame_.current(); }

    // The `.animation(...)` this element is inside, if any.
    void set_implicit_animation(std::shared_ptr<ImplicitAnimation> animation) noexcept {
        implicit_ = std::move(animation);
    }
    // `.animates_layout(false)`: frame changes always snap.
    void set_animates_layout(bool enabled) noexcept { animates_layout_ = enabled; }
    // The animation for a change applied now: the running transaction's, else an implicit one
    // that fired in this update.
    const Animation* change_animation() const noexcept;

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

    // How a parent layout sees this element: itself, or for If/ForEach the elements they show
    // right now (flattened), or nothing for EmptyView.
    virtual void collect_layout_children(std::vector<Element*>& out) { out.push_back(this); }
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
    // Tells the ancestors that the set of children a layout sees has changed.
    void notify_structure_changed() {
        if (parent_) {
            parent_->on_structure_changed();
        }
    }
    // A descendant If/ForEach changed what it shows. Layouts rebuild their child lists; other
    // elements pass it on.
    virtual void on_structure_changed() { notify_structure_changed(); }

    // Hits nothing by default (spacers, empty views). Containers test their children.
    virtual bool hit_impl(Point, std::vector<Element*>&) { return false; }

    virtual Size measure(ProposedSize proposal) = 0;
    // `bounds` is the element's frame.
    virtual void arrange(Rect bounds, ProposedSize proposal) = 0;
    virtual void paint_impl(DisplayList& list) const = 0;

private:
    void paint_focus_ring(DisplayList& list) const;

    Element* parent_ = nullptr;
    FocusManager* focus_manager_ = nullptr;
    std::function<Color()> focus_ring_color_;
    Animated<Rect> frame_{Rect{}};
    bool placed_ = false;
    bool animates_layout_ = true;
    bool snap_next_layout_ = false;
    std::shared_ptr<ImplicitAnimation> implicit_;
    std::vector<std::pair<ProposedSize, Size>> size_cache_;
};

class EmptyElement final : public Element {
public:
    // Takes no place (and no spacing) in a parent layout.
    void collect_layout_children(std::vector<Element*>&) override {}

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
    CornerRadii radii; // rounded_rectangle only
    std::optional<Prop<ShapeStyle>> fill;
    std::optional<ShapeStroke> stroke;
    std::shared_ptr<const ShapePath> path; // ShapeKind::path
};

// The rounded rect a shape occupies inside `frame`.
struct ShapeOutline {
    Rect rect;
    CornerRadii radii;
};
ShapeOutline shape_outline(const ShapeSpec& spec, Rect frame);

class ShapeElement final : public Element {
public:
    explicit ShapeElement(ShapeSpec spec) : spec_(std::move(spec)) {}

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
    bool hit_path(Point point) const;
    const Geometry& geometry(Size size, float scale) const;

    ShapeSpec spec_;
    bool has_fill_ = false;
    Animated<ShapeStyle> fill_{ShapeStyle(Color::clear)};
    Animated<Color> stroke_color_{Color::clear};
    mutable Geometry geometry_;
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

private:
    TextLayoutResult layout(ProposedSize proposal) const;

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
    TransitionElement(Transition transition, std::unique_ptr<Element> content)
        : transition_(std::move(transition)), content_(std::move(content)) {
        adopt(*content_);
    }

    void insert(const Animation* animation);
    // Starts the exit; returns false if there's nothing to animate (remove it right away).
    bool remove(const Animation* animation);
    bool exit_finished() const { return removing_ && !progress_.animating(); }

    TransitionElement* as_transition() noexcept override { return this; }
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

private:
    void rebuild_children();
    std::vector<Element*> paint_order() const;

    std::shared_ptr<const Layout> layout_;
    std::vector<std::unique_ptr<Element>> children_; // owned, as built
    std::vector<Element*> laid_out_;                 // what the layout sees: If/ForEach flattened
    std::vector<LayoutChild> handles_;
};

class LayoutModifierElement final : public Element {
public:
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
    EffectElement(Effects effects, std::unique_ptr<Element> content)
        : effects_(std::move(effects)), content_(std::move(content)) {
        adopt(*content_);
    }

    void set_opacity(float opacity, const Animation* animation) { opacity_.set(opacity, animation); }
    void set_offset_x(float x, const Animation* animation) { offset_x_.set(x, animation); }
    void set_offset_y(float y, const Animation* animation) { offset_y_.set(y, animation); }
    void set_z_index(float z) noexcept { z_index_ = z; }
    void for_each_child(const std::function<void(Element&)>& visit) override { visit(*content_); }

    float layout_priority() const noexcept override { return content_->layout_priority(); }
    float z_index() const noexcept override { return z_index_.value_or(content_->z_index()); }
    bool is_spacer() const noexcept override { return content_->is_spacer(); }

protected:
    Size measure(ProposedSize proposal) override { return content_->size_that_fits(proposal); }
    void arrange(Rect bounds, ProposedSize proposal) override { content_->place({bounds.x, bounds.y}, proposal); }
    void paint_impl(DisplayList& list) const override;
    bool hit_impl(Point point, std::vector<Element*>& path) override;

private:
    Effects effects_;
    std::unique_ptr<Element> content_;
    Animated<float> opacity_{1.f}; // resolved from effects_.opacity
    Animated<float> offset_x_{0.f};
    Animated<float> offset_y_{0.f};
    std::optional<float> z_index_; // .z_index(...)
};

class DecorationElement final : public Element {
public:
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
    bool hit_impl(Point point, std::vector<Element*>& path) override { return content_->hit_test(point, path); }

private:
    std::unique_ptr<EventHandler> handler_;
    std::unique_ptr<Element> content_;
    std::function<HoverDelay()> environment_delay_;
};

// A Button: behavior here, appearance from its ButtonStyle's body (the content). Hover and press
// are written to States the style reads, so only what the style derives from them updates.
class ButtonElement final : public Element, private EventHandler {
public:
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

    EventHandler* event_handler() noexcept override { return this; }
    void for_each_child(const std::function<void(Element&)>& visit) override { visit(*content_); }
    void reveal(Rect rect) override;
    std::optional<Rect> visible_rect() const override { return viewport_; }

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

    bool scrolls(Axis axis) const noexcept;
    ProposedSize content_proposal(ProposedSize proposal) const noexcept;
    Point max_offset() const noexcept;
    Point current_offset() const { return {offset_x_.current(), offset_y_.current()}; }
    // Scrolls to `target` (clamped to the content); false if that's where it already is.
    bool scroll_to(Point target, const Animation* animation);
    std::optional<Thumb> thumb(Axis axis) const;
    std::optional<Axis> thumb_at(Point point) const;
    void show_indicators();
    void fade_indicators();
    void apply_request();

    ScrollViewOptions options_;
    std::unique_ptr<Element> content_;
    std::function<Color()> indicator_color_;
    Rect viewport_;     // from the last arrange
    Size content_size_; // the content's ideal size there
    Point target_;      // where the offset is heading
    Animated<float> offset_x_{0.f};
    Animated<float> offset_y_{0.f};
    Animated<float> indicators_{0.f}; // scroll bar opacity
    bool hovered_ = false;
    std::optional<Axis> dragging_; // a scroll bar thumb being dragged
    Point drag_start_;
    Point drag_offset_;
};

} // namespace elg::detail

#endif // ELG_VIEW_ELEMENT_H
