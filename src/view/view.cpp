// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// View, its modifier sugar, the protocol base classes, and lowering.

#include "view/view_node.h"

#include "core/crash.h"
#include "view/text_field.h"

#include <elg/animation.h>
#include <elg/modifiers/decoration.h>
#include <elg/modifiers/effects.h>
#include <elg/modifiers/environment.h>
#include <elg/modifiers/events.h>
#include <elg/styles.h>
#include <elg/modifiers/layout.h>
#include <elg/shapes.h>
#include <elg/text.h>
#include <elg/views.h>

namespace elg {

using detail::ViewAccess;

// --- View ---

View::View() noexcept = default;

View::View(std::shared_ptr<const detail::ViewNode> node) noexcept : node_(std::move(node)) {}

View View::with_modifier(std::shared_ptr<const Modifier> modifier) const {
    return View(std::make_shared<const detail::ModifiedNode>(*this, std::move(modifier)));
}

View View::deferred(std::function<View()> body) {
    return View(std::make_shared<const detail::DeferredNode>(std::move(body)));
}

View View::padding(float length) const {
    return padding(EdgeInsets::all(length));
}

View View::padding(Edges edges, float length) const {
    const auto on = [&](Edges edge) { return contains(edges, edge) ? length : 0.f; };
    return padding(EdgeInsets{on(Edges::top), on(Edges::leading), on(Edges::bottom), on(Edges::trailing)});
}

View View::padding(EdgeInsets insets) const {
    return modifier(PaddingModifier(insets));
}

View View::frame(FrameOptions options) const {
    return modifier(FrameModifier(options));
}

View View::frame(float width, float height, Alignment alignment) const {
    return frame(FrameOptions{width, height, alignment});
}

View View::frame(FlexibleFrameOptions options) const {
    return modifier(FlexibleFrameModifier(options));
}

View View::fixed_size(bool horizontal, bool vertical) const {
    return modifier(FixedSizeModifier(horizontal, vertical));
}

View View::layout_priority(float priority) const {
    return modifier(LayoutPriorityModifier(priority));
}

View View::offset(Prop<float> x, Prop<float> y) const {
    return modifier(OffsetModifier(std::move(x), std::move(y)));
}

View View::background(View view, Alignment alignment) const {
    return modifier(BackgroundModifier(std::move(view), alignment));
}

View View::background(Prop<ShapeStyle> style) const {
    return background(Rectangle().fill(std::move(style)));
}

View View::background(Prop<ShapeStyle> style, const Shape& shape) const {
    return background(shape.fill(std::move(style)));
}

View View::clipped() const {
    return modifier(ClippedModifier());
}

View View::clip_shape(const Shape& shape) const {
    return modifier(ClipShapeModifier(shape));
}

View View::corner_radius(float radius) const {
    return clip_shape(RoundedRectangle(radius));
}

View View::opacity(Prop<float> opacity) const {
    return modifier(OpacityModifier(std::move(opacity)));
}

View View::shadow(Shadow shadow) const {
    return modifier(ShadowModifier(shadow));
}

View View::content_shape(const Shape& shape) const {
    return modifier(ContentShapeModifier(shape));
}

View View::allows_hit_testing(bool enabled) const {
    return modifier(AllowsHitTestingModifier(enabled));
}

View View::on_tap_gesture(std::function<void()> action, int count) const {
    return modifier(TapGestureModifier(count, std::move(action)));
}

View View::on_hover(std::function<void(bool)> action) const {
    return modifier(HoverModifier(std::move(action)));
}

View View::on_hover(std::function<void(bool)> action, HoverDelay delay) const {
    return modifier(HoverModifier(std::move(action), delay));
}

View View::hover_delay(HoverDelay delay) const {
    return modifier(HoverDelayModifier(delay));
}

View View::focusable(bool focusable) const {
    return modifier(FocusableModifier(focusable));
}

View View::focused(Binding<bool> is_focused) const {
    return View(std::make_shared<const detail::FocusedNode>(*this, std::move(is_focused)));
}

View View::on_key_press(Key key, std::function<void()> action) const {
    return on_key_press([key, action = std::move(action)](const KeyEvent& event) {
        if (event.key != key) {
            return false;
        }
        if (action) {
            action();
        }
        return true;
    });
}

View View::on_key_press(std::function<bool(const KeyEvent&)> action) const {
    return modifier(KeyPressModifier(std::move(action)));
}

View View::tint(Prop<Color> color) const {
    return modifier(TintModifier(std::move(color)));
}

View View::overlay(View view, Alignment alignment) const {
    return modifier(OverlayModifier(std::move(view), alignment));
}

View View::border(Color color, float width) const {
    return modifier(BorderModifier(color, width));
}

View View::font(Prop<Font> font) const {
    return modifier(FontModifier(std::move(font)));
}

View View::foreground_style(Prop<Color> color) const {
    return modifier(ForegroundStyleModifier(std::move(color)));
}

View View::color_scheme(Prop<ColorScheme> scheme) const {
    return modifier(ColorSchemeModifier(std::move(scheme)));
}

View View::theme(Theme theme) const {
    return modifier(ThemeModifier(theme));
}

View View::disabled(Prop<bool> disabled) const {
    return modifier(DisabledModifier(std::move(disabled)));
}

View View::disabled() const {
    return disabled(true);
}

View View::animation_impl(const Animation& animation, std::function<bool()> changed) const {
    return View(std::make_shared<const detail::AnimationNode>(*this, animation, std::move(changed)));
}

View View::animates_layout(bool enabled) const {
    return View(std::make_shared<const detail::LayoutAnimationNode>(*this, enabled));
}

View View::transition(const Transition& transition) const {
    return View(std::make_shared<const detail::TransitionNode>(*this, transition));
}

View View::button_style_impl(std::shared_ptr<const ButtonStyle> style) const {
    return modifier(ButtonStyleModifier(std::move(style)));
}

View View::toggle_style_impl(std::shared_ptr<const ToggleStyle> style) const {
    return modifier(ToggleStyleModifier(std::move(style)));
}

View View::text_field_style_impl(std::shared_ptr<const TextFieldStyle> style) const {
    return modifier(TextFieldStyleModifier(std::move(style)));
}

// --- Protocols ---

Modifier::~Modifier() = default;

LayoutChild::LayoutChild(detail::Element& element) noexcept : element_(&element) {}

Size LayoutChild::size_that_fits(ProposedSize proposal) const {
    return element_->size_that_fits(proposal);
}

void LayoutChild::place(Point position, ProposedSize proposal) const {
    element_->place(position, proposal);
}

float LayoutChild::layout_priority() const noexcept {
    return element_->layout_priority();
}

bool LayoutChild::is_spacer() const noexcept {
    return element_->is_spacer();
}

Layout::~Layout() = default;

View Layout::make_view(std::shared_ptr<const Layout> layout, std::vector<View> children) {
    return ViewAccess::make(std::make_shared<const detail::LayoutNode>(std::move(layout), std::move(children)));
}

// --- Built-in views ---

View Spacer(OptionalLength min_length) {
    return ViewAccess::make(std::make_shared<const detail::SpacerNode>(min_length.value_or(default_stack_spacing)));
}

View EmptyView() {
    return View();
}

View Canvas(std::function<void(GraphicsContext&)> draw) {
    return ViewAccess::make(std::make_shared<const detail::CanvasNode>(std::move(draw)));
}

View Button(View label, std::function<void()> action) {
    return ViewAccess::make(std::make_shared<const detail::ButtonNode>(std::move(label), std::move(action)));
}

View Button(std::string title, std::function<void()> action) {
    return Button(Text(std::move(title)), std::move(action));
}

View TextField(std::string placeholder, Binding<std::string> text, TextFieldOptions options) {
    return ViewAccess::make(
        std::make_shared<const detail::TextFieldNode>(std::move(placeholder), std::move(text), std::move(options)));
}

View ScrollView(View content) {
    return ScrollView({}, std::move(content));
}

View ScrollView(ScrollViewOptions options, View content) {
    return ViewAccess::make(std::make_shared<const detail::ScrollNode>(options, std::move(content)));
}

View Toggle(View label, Binding<bool> is_on) {
    return ViewAccess::make(std::make_shared<const detail::ToggleNode>(std::move(label), std::move(is_on)));
}

View Toggle(std::string title, Binding<bool> is_on) {
    return Toggle(Text(std::move(title)), std::move(is_on));
}

View If(Prop<bool> condition, View then_view, View else_view) {
    return ViewAccess::make(
        std::make_shared<const detail::IfNode>(std::move(condition), std::move(then_view), std::move(else_view)));
}

View detail::make_for_each(std::function<std::vector<ForEachItem>()> items) {
    return ViewAccess::make(std::make_shared<const ForEachNode>(std::move(items)));
}

} // namespace elg

namespace elg::detail {

// --- Lowering ---

namespace {
const EnvironmentValues* g_environment = nullptr;
} // namespace

EnvironmentScope::EnvironmentScope(const EnvironmentValues& environment) noexcept : previous_(g_environment) {
    g_environment = &environment;
}

EnvironmentScope::~EnvironmentScope() {
    g_environment = previous_;
}

const EnvironmentValues* current_environment() noexcept {
    return g_environment;
}

std::unique_ptr<Element> lower(const View& view, const LowerContext& context) {
    const std::shared_ptr<const ViewNode>& node = ViewAccess::node(view);
    if (!node) {
        return std::make_unique<EmptyElement>();
    }
    const EnvironmentScope scope(context.environment);
    std::unique_ptr<Element> element = node->lower(context);
    if (context.implicit_animation) {
        element->set_implicit_animation(context.implicit_animation);
    }
    if (!context.animates_layout) {
        element->set_animates_layout(false);
    }
    return element;
}

std::unique_ptr<Element> AnimationNode::lower(const LowerContext& context) const {
    auto implicit = std::make_shared<ImplicitAnimation>(ImplicitAnimation{animation_});
    // Runs before other effects in a flush, so the property effects it should animate see it
    // fired.
    start_effect(std::make_shared<EffectNode>([implicit, changed = changed_] {
                     if (changed()) {
                         implicit->fired_flush = current_flush();
                     }
                 }),
                 true);
    LowerContext inner = context;
    inner.implicit_animation = std::move(implicit);
    return detail::lower(content_, inner);
}

std::unique_ptr<Element> TransitionNode::lower(const LowerContext& context) const {
    return std::make_unique<TransitionElement>(transition_, detail::lower(content_, context));
}

namespace {

std::function<HoverDelay()> environment_hover_delay(const EnvironmentValues& environment) {
    return [environment] { return environment.get<HoverDelayKey>(); };
}

// Focusable elements draw the ring in the tint.
void join_focus(Element& element, const LowerContext& context) {
    if (context.focus) {
        element.set_focus_manager(context.focus,
                                  [environment = context.environment] { return environment.get<TintKey>(); });
    }
}

std::unique_ptr<EventElement> make_event_element(std::unique_ptr<EventHandler> handler, const View& content,
                                                 const LowerContext& context) {
    auto element = std::make_unique<EventElement>(std::move(handler), detail::lower(content, context),
                                                  environment_hover_delay(context.environment));
    join_focus(*element, context);
    return element;
}

// Buttons and toggles: `body` (from the style) with button behavior; `pressed` and `hovered` are
// the States the style reads.
std::unique_ptr<Element> make_button_element(const View& body, std::function<void()> action, State<bool> pressed,
                                             State<bool> hovered, const LowerContext& context) {
    const EnvironmentValues environment = context.environment;
    auto element = std::make_unique<ButtonElement>(
        detail::lower(body, context.nested()), std::move(action), std::move(pressed), std::move(hovered),
        [environment] { return !environment.get<DisabledKey>(); }, environment_hover_delay(environment));
    join_focus(*element, context);
    return element;
}

// `.focused(binding)`: writes focus changes to the binding.
class FocusBindingHandler final : public EventHandler {
public:
    explicit FocusBindingHandler(Binding<bool> binding) : binding_(std::move(binding)) {}
    bool focusable() const override { return true; }
    EventResult focus(bool focused) override {
        binding_.set(focused);
        return {};
    }

private:
    Binding<bool> binding_;
};

} // namespace

std::unique_ptr<Element> ModifiedNode::lower(const LowerContext& context) const {
    switch (modifier_->protocol()) {
    case ModifierProtocol::view:
        return detail::lower(static_cast<const ViewModifier&>(*modifier_).body(content_), context);
    case ModifierProtocol::layout:
        return std::make_unique<LayoutModifierElement>(std::static_pointer_cast<const LayoutModifier>(modifier_),
                                                       detail::lower(content_, context));
    case ModifierProtocol::decoration: {
        auto decoration = std::static_pointer_cast<const DecorationModifier>(modifier_);
        auto content = detail::lower(content_, context);
        auto secondary = detail::lower(decoration->decoration(), context.nested());
        return std::make_unique<DecorationElement>(std::move(decoration), std::move(content), std::move(secondary));
    }
    case ModifierProtocol::effect: {
        Effects effects;
        static_cast<const EffectModifier&>(*modifier_).apply(effects);
        const Prop<float> opacity = effects.opacity;
        const Prop<float> offset_x = effects.offset_x;
        const Prop<float> offset_y = effects.offset_y;
        auto element = std::make_unique<EffectElement>(std::move(effects), detail::lower(content_, context));
        EffectElement* const target = element.get();
        detail::bind(opacity, context, [target](float value, const Animation* animation) { target->set_opacity(value, animation); });
        detail::bind(offset_x, context, [target](float value, const Animation* animation) { target->set_offset_x(value, animation); });
        detail::bind(offset_y, context, [target](float value, const Animation* animation) { target->set_offset_y(value, animation); });
        return element;
    }
    case ModifierProtocol::event:
        return make_event_element(static_cast<const EventModifier&>(*modifier_).make_handler(), content_, context);
    case ModifierProtocol::environment: {
        LowerContext inner = context;
        static_cast<const EnvironmentModifier&>(*modifier_).apply(inner.environment);
        return detail::lower(content_, inner);
    }
    }
    internal_error("unknown modifier protocol");
}

std::unique_ptr<Element> LayoutNode::lower(const LowerContext& context) const {
    LowerContext child_context = context;
    child_context.spacer_axis = layout_->spacer_axis();
    std::vector<std::unique_ptr<Element>> children;
    children.reserve(children_.size());
    for (const View& child : children_) {
        children.push_back(detail::lower(child, child_context));
    }
    return std::make_unique<LayoutElement>(layout_, std::move(children));
}

std::unique_ptr<Element> ShapeNode::lower(const LowerContext& context) const {
    auto element = std::make_unique<ShapeElement>(spec_);
    ShapeElement* const target = element.get();
    if (spec_.fill) {
        detail::bind(*spec_.fill, context,
             [target](ShapeStyle fill, const Animation* animation) { target->set_fill(std::move(fill), animation); });
    }
    if (spec_.stroke) {
        detail::bind(spec_.stroke->color, context,
             [target](Color color, const Animation* animation) { target->set_stroke_color(color, animation); });
    } else if (!spec_.fill) {
        // Unfilled, unstroked shapes use the environment's foreground.
        const EnvironmentValues environment = context.environment;
        detail::bind(Prop<ShapeStyle>([environment] { return ShapeStyle(environment.get<ForegroundKey>()); }), context,
             [target](ShapeStyle fill, const Animation* animation) { target->set_fill(std::move(fill), animation); });
    }
    return element;
}

std::unique_ptr<Element> TextNode::lower(const LowerContext& context) const {
    ELG_ASSERT(context.text, "lowering Text without a text system");
    const EnvironmentValues environment = context.environment;
    auto element = std::make_unique<TextElement>(*context.text, style_.line_limit, style_.alignment);
    TextElement* const target = element.get();

    // The font: the Text's own, else the environment's; then any weight override.
    Prop<Font> font = style_.font ? Prop<Font>(*style_.font)
                                  : Prop<Font>([environment] { return environment.get<FontKey>(); });
    if (style_.weight) {
        font = Prop<Font>([font, weight = *style_.weight] { return font.get().weight(weight); });
    }
    detail::bind(font, context, [target](const Font& value) { target->set_font(value); });
    detail::bind(content_, context, [target](const std::string& content) { target->set_content(content); });
    detail::bind(style_.color ? *style_.color : Prop<Color>([environment] { return environment.get<ForegroundKey>(); }),
         context, [target](Color color, const Animation* animation) { target->set_color(color, animation); });
    return element;
}

std::unique_ptr<Element> ButtonNode::lower(const LowerContext& context) const {
    // The button owns the interaction state; its style turns that into an appearance.
    const State<bool> pressed(false);
    const State<bool> hovered(false);
    const EnvironmentValues environment = context.environment;
    const auto enabled = [environment] { return !environment.get<DisabledKey>(); };
    const ButtonConfiguration configuration{
        .label = label_,
        .is_pressed = [pressed] { return pressed(); },
        .is_hovered = [hovered] { return hovered(); },
        .is_enabled = enabled,
    };

    // The style is chosen when the button is built.
    const std::shared_ptr<const ButtonStyle> style = untrack([&] { return environment.get<ButtonStyleKey>(); });
    View body;
    {
        const EnvironmentScope scope(environment); // Environment<K>() in make_body
        body = style->make_body(configuration);
    }
    return make_button_element(body, action_, pressed, hovered, context);
}

namespace {

// Where a TextField's style puts the text.
class TextEditorNode final : public ViewNode {
public:
    explicit TextEditorNode(std::shared_ptr<TextEditor> editor) : editor_(std::move(editor)) {}
    std::unique_ptr<Element> lower(const LowerContext&) const override {
        return std::make_unique<TextEditorElement>(editor_);
    }

private:
    std::shared_ptr<TextEditor> editor_;
};

} // namespace

std::unique_ptr<Element> TextFieldNode::lower(const LowerContext& context) const {
    ELG_ASSERT(context.text, "lowering a TextField without a text system");
    const EnvironmentValues environment = context.environment;
    const auto editor = std::make_shared<TextEditor>(*context.text, text_, placeholder_, options_.on_submit,
                                                     context.services, context.focus, environment);
    // The font follows the environment's; the text follows the binding (when changed elsewhere).
    detail::bind(Prop<Font>([environment] { return environment.get<FontKey>(); }), context,
         [editor](const Font& font) { editor->set_font(font); });
    detail::bind(Prop<std::string>(text_), context,
         [editor](const std::string& value) { editor->set_text(value); });

    const State<bool> focused(false);
    const State<bool> hovered(false);
    const auto enabled = [environment] { return !environment.get<DisabledKey>(); };
    const TextFieldConfiguration configuration{
        .field = ViewAccess::make(std::make_shared<const TextEditorNode>(editor)),
        .is_focused = [focused] { return focused(); },
        .is_hovered = [hovered] { return hovered(); },
        .is_enabled = enabled,
    };
    const std::shared_ptr<const TextFieldStyle> style = untrack([&] { return environment.get<TextFieldStyleKey>(); });
    View body;
    {
        const EnvironmentScope scope(environment);
        body = style->make_body(configuration);
    }
    auto element = std::make_unique<TextFieldElement>(editor, detail::lower(body, context.nested()), focused, hovered,
                                                      enabled, context.services);
    if (context.focus) {
        element->set_focus_manager(context.focus, {}); // the style shows focus
    }
    return element;
}

std::unique_ptr<Element> ScrollNode::lower(const LowerContext& context) const {
    const EnvironmentValues environment = context.environment;
    return std::make_unique<ScrollElement>(options_, detail::lower(content_, context.nested()),
                                           [environment] { return environment.color(ThemeColor::secondary); });
}

std::unique_ptr<Element> ToggleNode::lower(const LowerContext& context) const {
    // A button whose action flips the binding; the ToggleStyle draws it.
    const State<bool> pressed(false);
    const State<bool> hovered(false);
    const EnvironmentValues environment = context.environment;
    const Binding<bool> is_on = is_on_;
    const auto toggle = [is_on] {
        with_animation(Animation::smooth(0.3), [&] { is_on.set(!untrack([&] { return is_on(); })); });
    };
    const ToggleConfiguration configuration{
        .label = label_,
        .is_on = [is_on] { return is_on(); },
        .toggle = toggle,
        .is_pressed = [pressed] { return pressed(); },
        .is_hovered = [hovered] { return hovered(); },
        .is_enabled = [environment] { return !environment.get<DisabledKey>(); },
    };

    const std::shared_ptr<const ToggleStyle> style = untrack([&] { return environment.get<ToggleStyleKey>(); });
    View body;
    {
        const EnvironmentScope scope(environment);
        body = style->make_body(configuration);
    }
    return make_button_element(body, toggle, pressed, hovered, context);
}

std::unique_ptr<Element> FocusedNode::lower(const LowerContext& context) const {
    auto element = make_event_element(std::make_unique<FocusBindingHandler>(is_focused_), content_, context);
    if (!context.focus) {
        return element;
    }
    // Binding -> focus. (Focus -> binding is the handler.) The focus manager is updated before the
    // handlers hear about it, so the binding write they cause doesn't bounce back here.
    effect([binding = is_focused_, target = element.get(), focus = context.focus, needs_frame = context.needs_frame] {
        const bool wanted = binding();
        const bool has_focus = focus->focused() == target;
        if (wanted != has_focus) {
            focus->focus(wanted ? target : nullptr, true);
            if (needs_frame) {
                *needs_frame = true;
            }
        }
    });
    return element;
}

std::unique_ptr<Element> SpacerNode::lower(const LowerContext& context) const {
    return std::make_unique<SpacerElement>(min_length_, context.spacer_axis);
}

std::unique_ptr<Element> CanvasNode::lower(const LowerContext&) const {
    return std::make_unique<CanvasElement>(draw_);
}

std::unique_ptr<Element> DeferredNode::lower(const LowerContext& context) const {
    return detail::lower(body_(), context);
}

} // namespace elg::detail
