// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_VIEW_VIEW_NODE_H
#define ELG_VIEW_VIEW_NODE_H

// What a View holds: an immutable description node. Modifiers wrap nodes (like SwiftUI's
// ModifiedContent), so views are cheap to copy and share. Lowering turns nodes into elements.

#include "view/element.h"

#include <elg/environment.h>
#include <elg/layout.h>
#include <elg/modifier.h>
#include <elg/prop.h>
#include <elg/reactive.h>
#include <elg/view.h>
#include <elg/views.h>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace elg::detail {

struct LowerContext {
    TextSystem* text = nullptr;
    // Set to true to request a new frame for the window being built (reactive updates).
    bool* needs_frame = nullptr;
    // Set to true when If/ForEach add or remove elements (the event dispatcher must forget them).
    bool* tree_changed = nullptr;
    EnvironmentValues environment;
    // Set for the direct children of a layout with a spacer axis (stacks).
    std::optional<Axis> spacer_axis;

    // The context for a view that isn't a direct child of the current layout.
    LowerContext nested() const {
        LowerContext copy = *this;
        copy.spacer_axis.reset();
        return copy;
    }
};

class ViewNode {
public:
    virtual ~ViewNode() = default;
    virtual std::unique_ptr<Element> lower(const LowerContext& context) const = 0;
};

struct ViewAccess {
    static const std::shared_ptr<const ViewNode>& node(const View& view) noexcept { return view.node_; }
    static View make(std::shared_ptr<const ViewNode> node) noexcept { return View(std::move(node)); }
};

// Mounts a view: builds its element tree. Runs component bodies and ViewModifier bodies.
// Effects created meanwhile (reactive arguments, effect()) belong to the current Scope.
std::unique_ptr<Element> lower(const View& view, const LowerContext& context);

// Applies a reactive argument to an element (design.md §9.3): a static value is applied once; a
// reactive one becomes an effect that re-applies it, and requests a frame, whenever the state it
// reads changes.
template <class T, class Apply>
void bind(const Prop<T>& prop, const LowerContext& context, Apply apply) {
    if (const T* value = prop.static_value()) {
        apply(*value);
        return;
    }
    effect([prop, apply = std::move(apply), needs_frame = context.needs_frame] {
        apply(prop.get());
        if (needs_frame) {
            *needs_frame = true;
        }
    });
}

class ModifiedNode final : public ViewNode {
public:
    ModifiedNode(View content, std::shared_ptr<const Modifier> modifier)
        : content_(std::move(content)), modifier_(std::move(modifier)) {}
    std::unique_ptr<Element> lower(const LowerContext& context) const override;

private:
    View content_;
    std::shared_ptr<const Modifier> modifier_;
};

class LayoutNode final : public ViewNode {
public:
    LayoutNode(std::shared_ptr<const Layout> layout, std::vector<View> children)
        : layout_(std::move(layout)), children_(std::move(children)) {}
    std::unique_ptr<Element> lower(const LowerContext& context) const override;

private:
    std::shared_ptr<const Layout> layout_;
    std::vector<View> children_;
};

class ShapeNode final : public ViewNode {
public:
    explicit ShapeNode(ShapeSpec spec) : spec_(std::move(spec)) {}
    std::unique_ptr<Element> lower(const LowerContext& context) const override;
    const ShapeSpec& spec() const noexcept { return spec_; }

private:
    ShapeSpec spec_;
};

class TextNode final : public ViewNode {
public:
    TextNode(Prop<std::string> content, TextStyle style) : content_(std::move(content)), style_(std::move(style)) {}
    std::unique_ptr<Element> lower(const LowerContext& context) const override;
    const Prop<std::string>& content() const noexcept { return content_; }
    const TextStyle& style() const noexcept { return style_; }

private:
    Prop<std::string> content_;
    TextStyle style_;
};

class ButtonNode final : public ViewNode {
public:
    ButtonNode(View label, std::function<void()> action) : label_(std::move(label)), action_(std::move(action)) {}
    std::unique_ptr<Element> lower(const LowerContext& context) const override;

private:
    View label_;
    std::function<void()> action_;
};

class IfNode final : public ViewNode {
public:
    IfNode(Prop<bool> condition, View then_view, View else_view)
        : condition_(std::move(condition)), then_(std::move(then_view)), else_(std::move(else_view)) {}
    std::unique_ptr<Element> lower(const LowerContext& context) const override;

private:
    Prop<bool> condition_;
    View then_;
    View else_;
};

class ForEachNode final : public ViewNode {
public:
    explicit ForEachNode(std::function<std::vector<ForEachItem>()> items) : items_(std::move(items)) {}
    std::unique_ptr<Element> lower(const LowerContext& context) const override;

private:
    std::function<std::vector<ForEachItem>()> items_;
};

// If/ForEach: a dynamic list of children, each mounted in its own Scope so that removing it
// ends its effects and state. Inside a layout its children are the layout's children
// (collect_layout_children); on its own it stacks them vertically.
class FragmentElement final : public Element {
public:
    explicit FragmentElement(LowerContext context) : context_(std::move(context)) {}

    // Shows exactly `items`, in order. Children whose key and value are unchanged are kept.
    void set_items(const std::vector<ForEachItem>& items);

    void collect_layout_children(std::vector<Element*>& out) override;
    void for_each_child(const std::function<void(Element&)>& visit) override;

protected:
    Size measure(ProposedSize proposal) override;
    void arrange(Rect bounds, ProposedSize proposal) override;
    void paint_impl(DisplayList& list) const override;
    bool hit_impl(Point point, std::vector<Element*>& path) override;
    void on_structure_changed() override;

private:
    // Declaration order matters: the scope is destroyed before the element, so no effect can run
    // against a destroyed element.
    struct Child {
        ItemKey key;
        std::shared_ptr<const void> value;
        std::unique_ptr<Element> element;
        std::unique_ptr<Scope> scope;
    };

    Child mount(const ForEachItem& item);
    void rebuild_children();

    LowerContext context_;
    std::vector<Child> children_;
    std::vector<Element*> laid_out_;
    std::vector<LayoutChild> handles_;
    VStackLayout standalone_layout_;
};

class SpacerNode final : public ViewNode {
public:
    explicit SpacerNode(float min_length) : min_length_(min_length) {}
    std::unique_ptr<Element> lower(const LowerContext& context) const override;

private:
    float min_length_;
};

class CanvasNode final : public ViewNode {
public:
    explicit CanvasNode(std::function<void(GraphicsContext&)> draw) : draw_(std::move(draw)) {}
    std::unique_ptr<Element> lower(const LowerContext& context) const override;

private:
    std::function<void(GraphicsContext&)> draw_;
};

// A component or other view whose body is produced at mount time.
class DeferredNode final : public ViewNode {
public:
    explicit DeferredNode(std::function<View()> body) : body_(std::move(body)) {}
    std::unique_ptr<Element> lower(const LowerContext& context) const override;

private:
    std::function<View()> body_;
};

} // namespace elg::detail

#endif // ELG_VIEW_VIEW_NODE_H
