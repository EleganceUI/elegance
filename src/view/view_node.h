// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_VIEW_VIEW_NODE_H
#define ELG_VIEW_VIEW_NODE_H

// What a View holds: an immutable description node. Modifiers wrap nodes (like SwiftUI's
// ModifiedContent), so views are cheap to copy and share. Lowering turns nodes into elements.

#include "view/element.h"
#include "view/focus.h"
#include "view/window_services.h"

#include <elg/animation.h>
#include <elg/environment.h>
#include <elg/layout.h>
#include <elg/modifier.h>
#include <elg/prop.h>
#include <elg/reactive.h>
#include <elg/view.h>
#include <elg/views.h>

#include <functional>
#include <memory>
#include <utility>
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
    // The innermost `.animation(...)` around the views being built.
    std::shared_ptr<ImplicitAnimation> implicit_animation;
    // false inside `.animates_layout(false)`.
    bool animates_layout = true;
    // The window's keyboard focus.
    FocusManager* focus = nullptr;
    // The window's text input, clipboard, cursor, and timed redraws.
    WindowServices* services = nullptr;

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
// Setters taking (value, const Animation*) are animatable: they get the animation of the update
// that changed the value (an animated transaction's, or an implicit `.animation(...)`'s), or
// nullptr to jump.
template <class T, class Apply>
void bind(const Prop<T>& prop, const LowerContext& context, Apply apply) {
    constexpr bool animatable = std::invocable<Apply&, T, const Animation*>;
    if (const T* value = prop.static_value()) {
        if constexpr (animatable) {
            apply(*value, nullptr);
        } else {
            apply(*value);
        }
        return;
    }
    // Environment-dependent props (e.g. ThemeColor) resolve against the environment where they're
    // used; reading it is tracked, so a theme or color scheme change re-applies them.
    // The first value is where a new element starts, never animated: a view mounted inside
    // with_animation (a window opening) starts at its offset instead of sliding there from 0.
    effect([prop, apply = std::move(apply), environment = context.environment, needs_frame = context.needs_frame,
            implicit = context.implicit_animation, first = std::make_shared<bool>(true)] {
        T value = prop.get(environment);
        if constexpr (animatable) {
            const Animation* animation = current_animation();
            if (!animation && implicit && implicit->fired()) {
                animation = &implicit->animation;
            }
            if (std::exchange(*first, false)) {
                animation = nullptr;
            }
            apply(std::move(value), animation);
        } else {
            apply(std::move(value));
        }
        if (needs_frame) {
            *needs_frame = true;
        }
    });
}

// Joins `element` to the window's focus, drawing the ring in the tint.
void join_focus(Element& element, const LowerContext& context);
// A ButtonElement around `body` (from a style); `pressed` and `hovered` are what the style reads.
std::unique_ptr<ButtonElement> make_button_element(const View& body, std::function<void()> action, State<bool> pressed,
                                                   State<bool> hovered, const LowerContext& context);

// Makes Environment<K>() work while a node is lowered (bodies, styles).
class EnvironmentScope {
public:
    explicit EnvironmentScope(const EnvironmentValues& environment) noexcept;
    ~EnvironmentScope();
    EnvironmentScope(const EnvironmentScope&) = delete;
    EnvironmentScope& operator=(const EnvironmentScope&) = delete;

private:
    const EnvironmentValues* previous_;
};

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

class ScrollNode final : public ViewNode {
public:
    ScrollNode(ScrollViewOptions options, View content) : options_(options), content_(std::move(content)) {}
    std::unique_ptr<Element> lower(const LowerContext& context) const override;

private:
    ScrollViewOptions options_;
    View content_;
};

class TextFieldNode final : public ViewNode {
public:
    // `area`: a TextArea, whose options are a TextField's that don't apply (secure, line_limit).
    TextFieldNode(Prop<std::string> placeholder, Binding<std::string> text, TextFieldOptions options, bool area)
        : placeholder_(std::move(placeholder)), text_(std::move(text)), options_(std::move(options)), area_(area) {}
    std::unique_ptr<Element> lower(const LowerContext& context) const override;

private:
    Prop<std::string> placeholder_;
    Binding<std::string> text_;
    TextFieldOptions options_;
    bool area_;
};

// Toggle and Checkbox: one control, a button that flips a binding, with each one's style and
// accessibility role. A check box can be mixed.
class ToggleNode final : public ViewNode {
public:
    enum class Kind { toggle, checkbox };
    ToggleNode(Kind kind, View label, Binding<bool> value, Prop<bool> mixed = false)
        : kind_(kind), label_(std::move(label)), value_(std::move(value)), mixed_(std::move(mixed)) {}
    std::unique_ptr<Element> lower(const LowerContext& context) const override;

private:
    Kind kind_;
    View label_;
    Binding<bool> value_;
    Prop<bool> mixed_;
};

// `.focused(binding)`.
// `.on_activate(action)`: clicks, Space/Enter while focused, and the accessibility action.
class ActivateNode final : public ViewNode {
public:
    ActivateNode(View content, std::function<void()> action)
        : content_(std::move(content)), action_(std::move(action)) {}
    std::unique_ptr<Element> lower(const LowerContext& context) const override;

private:
    View content_;
    std::function<void()> action_;
};

class FocusedNode final : public ViewNode {
public:
    FocusedNode(View content, Binding<bool> is_focused) : content_(std::move(content)), is_focused_(std::move(is_focused)) {}
    std::unique_ptr<Element> lower(const LowerContext& context) const override;

private:
    View content_;
    Binding<bool> is_focused_;
};

// `.animation(animation, value)`: the subtree's changes animate when the value changed.
class AnimationNode final : public ViewNode {
public:
    AnimationNode(View content, Animation animation, std::function<bool()> changed)
        : content_(std::move(content)), animation_(animation), changed_(std::move(changed)) {}
    std::unique_ptr<Element> lower(const LowerContext& context) const override;

private:
    View content_;
    Animation animation_;
    std::function<bool()> changed_;
};

class LayoutAnimationNode final : public ViewNode {
public:
    LayoutAnimationNode(View content, bool enabled) : content_(std::move(content)), enabled_(enabled) {}
    std::unique_ptr<Element> lower(const LowerContext& context) const override {
        LowerContext inner = context;
        inner.animates_layout = enabled_;
        return detail::lower(content_, inner);
    }

private:
    View content_;
    bool enabled_;
};

class TransitionNode final : public ViewNode {
public:
    TransitionNode(View content, Transition transition) : content_(std::move(content)), transition_(std::move(transition)) {}
    std::unique_ptr<Element> lower(const LowerContext& context) const override;

private:
    View content_;
    Transition transition_;
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
    const std::function<std::vector<ForEachItem>()>& items() const noexcept { return items_; }

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
    bool flattened_by_layouts() const noexcept override { return true; }
    void for_each_child(const std::function<void(Element&)>& visit) override;
    void collect_exiting(std::vector<Element*>& out) override;
    void prune_exiting() override;
    bool always_arranges() const override { return !exiting_.empty(); } // prunes finished exits

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
        std::unique_ptr<Element> element; // a TransitionElement
        TransitionElement* transition = nullptr;
        std::unique_ptr<Scope> scope;
    };

    Child mount(const ForEachItem& item, const Animation* animation);
    void rebuild_children();

    LowerContext context_;
    std::vector<Child> children_;
    std::vector<Child> exiting_; // removed, animating out
    bool initialized_ = false;
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
