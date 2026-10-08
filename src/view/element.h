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

#include <elg/events.h>
#include <elg/graphics_context.h>
#include <elg/layout.h>
#include <elg/modifier.h>
#include <elg/prop.h>
#include <elg/text.h>
#include <elg/view.h>

#include <functional>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace elg::detail {

class Element {
public:
    virtual ~Element() = default;

    // Memoized per proposal until invalidate_layout().
    Size size_that_fits(ProposedSize proposal);
    // Sizes the element for `proposal`, positions it, and lays out its children.
    void place(Point position, ProposedSize proposal);
    void paint(DisplayList& list) const { paint_impl(list); }

    const Rect& frame() const noexcept { return frame_; }

    virtual float layout_priority() const noexcept { return 0; }
    virtual bool is_spacer() const noexcept { return false; }

    // Hit testing (design.md §10): true if `point` (window points) hits this element's content.
    // On a hit, `path` holds the elements from this one down to the innermost hit element.
    bool hit_test(Point point, std::vector<Element*>& path);
    // The handler for input to this element, if it has one.
    virtual EventHandler* event_handler() noexcept { return nullptr; }

    // Something that affects this element's size changed (e.g. reactive text): drops the cached
    // sizes of this element and every ancestor.
    void invalidate_layout() noexcept;

    // How a parent layout sees this element: itself, or for If/ForEach the elements they show
    // right now (flattened), or nothing for EmptyView.
    virtual void collect_layout_children(std::vector<Element*>& out) { out.push_back(this); }
    // Calls `visit` for each child this element owns.
    virtual void for_each_child(const std::function<void(Element&)>&) {}

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
    Element* parent_ = nullptr;
    Rect frame_;
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

enum class ShapeKind { rectangle, rounded_rectangle, capsule, circle };

struct ShapeStroke {
    Color color;
    float width = 1;
    bool centered = true; // false: inside the edge
};

// A Shape view's description (not to be confused with the public elg::ShapeStyle, its fill).
struct ShapeSpec {
    ShapeKind kind = ShapeKind::rectangle;
    CornerRadii radii; // rounded_rectangle only
    std::optional<Prop<ShapeStyle>> fill;
    std::optional<ShapeStroke> stroke;
};

// The rounded rect a shape occupies inside `frame`.
struct ShapeOutline {
    Rect rect;
    CornerRadii radii;
};
ShapeOutline shape_outline(const ShapeSpec& spec, Rect frame);

class ShapeElement final : public Element {
public:
    // `default_fill` (the environment's foreground) is used when the shape sets neither a fill
    // nor a stroke.
    ShapeElement(ShapeSpec spec, Color default_fill) : spec_(std::move(spec)), default_fill_(default_fill) {}

    // The resolved fill (static, or the latest value of a reactive one).
    void set_fill(ShapeStyle fill) { fill_ = std::move(fill); }

protected:
    Size measure(ProposedSize proposal) override;
    void arrange(Rect, ProposedSize) override {}
    void paint_impl(DisplayList& list) const override;
    bool hit_impl(Point point, std::vector<Element*>& path) override;

private:
    ShapeSpec spec_;
    Color default_fill_;
    std::optional<ShapeStyle> fill_;
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
    // Throws elg::ResourceError if the font can't be loaded. Content and color are set with the
    // setters (from static values or reactive arguments).
    TextElement(TextSystem& text, const Font& font, int line_limit, TextAlignment alignment);

    // Reshapes the text and invalidates layout.
    void set_content(std::string_view content);
    void set_color(Color color) noexcept { color_ = color; }

protected:
    Size measure(ProposedSize proposal) override;
    void arrange(Rect bounds, ProposedSize proposal) override;
    void paint_impl(DisplayList& list) const override;
    bool hit_impl(Point point, std::vector<Element*>&) override;

private:
    TextLayoutResult layout(ProposedSize proposal) const;

    TextSystem* text_;
    const FontFace* face_;
    float size_;
    Color color_ = Color::black;
    int line_limit_;
    TextAlignment alignment_;
    float ascent_;
    float line_height_;
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

    void set_opacity(float opacity) noexcept { opacity_ = opacity; }
    void for_each_child(const std::function<void(Element&)>& visit) override { visit(*content_); }

    float layout_priority() const noexcept override { return content_->layout_priority(); }
    bool is_spacer() const noexcept override { return content_->is_spacer(); }

protected:
    Size measure(ProposedSize proposal) override { return content_->size_that_fits(proposal); }
    void arrange(Rect bounds, ProposedSize proposal) override { content_->place({bounds.x, bounds.y}, proposal); }
    void paint_impl(DisplayList& list) const override;
    bool hit_impl(Point point, std::vector<Element*>& path) override;

private:
    Effects effects_;
    std::unique_ptr<Element> content_;
    float opacity_ = 1; // resolved from effects_.opacity
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
    EventElement(std::unique_ptr<EventHandler> handler, std::unique_ptr<Element> content)
        : handler_(std::move(handler)), content_(std::move(content)) {
        adopt(*content_);
    }

    float layout_priority() const noexcept override { return content_->layout_priority(); }
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
};

// The built-in push button: its label on a tinted rounded rect that reacts to hover and press.
class ButtonElement final : public Element, private EventHandler {
public:
    ButtonElement(std::unique_ptr<Element> label, std::function<void()> action, Color tint)
        : label_(std::move(label)), action_(std::move(action)), tint_(tint) {
        adopt(*label_);
    }

    EventHandler* event_handler() noexcept override { return this; }
    void for_each_child(const std::function<void(Element&)>& visit) override { visit(*label_); }

protected:
    Size measure(ProposedSize proposal) override;
    void arrange(Rect bounds, ProposedSize proposal) override;
    void paint_impl(DisplayList& list) const override;
    bool hit_impl(Point point, std::vector<Element*>&) override;

private:
    EventResult pointer(const PointerEvent& event, Rect frame) override;
    EventResult hover(bool inside) override;

    std::unique_ptr<Element> label_;
    std::function<void()> action_;
    Color tint_;
    bool hovered_ = false;
    bool pressed_ = false;
};

} // namespace elg::detail

#endif // ELG_VIEW_ELEMENT_H
