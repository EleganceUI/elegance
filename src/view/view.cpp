// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// View, its modifier sugar, the protocol base classes, and lowering.

#include "view/view_node.h"

#include "core/crash.h"

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

View View::offset(float x, float y) const {
    return modifier(OffsetModifier(x, y));
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

View View::button_style_impl(std::shared_ptr<const ButtonStyle> style) const {
    return modifier(ButtonStyleModifier(std::move(style)));
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
    return node->lower(context);
}

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
        auto element = std::make_unique<EffectElement>(std::move(effects), detail::lower(content_, context));
        bind(opacity, context, [target = element.get()](float value) { target->set_opacity(value); });
        return element;
    }
    case ModifierProtocol::event:
        return std::make_unique<EventElement>(static_cast<const EventModifier&>(*modifier_).make_handler(),
                                              detail::lower(content_, context));
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
        bind(*spec_.fill, context, [target](ShapeStyle fill) { target->set_fill(std::move(fill)); });
    }
    if (spec_.stroke) {
        bind(spec_.stroke->color, context, [target](Color color) { target->set_stroke_color(color); });
    } else if (!spec_.fill) {
        // Unfilled, unstroked shapes use the environment's foreground.
        const EnvironmentValues environment = context.environment;
        bind(Prop<ShapeStyle>([environment] { return ShapeStyle(environment.get<ForegroundKey>()); }), context,
             [target](ShapeStyle fill) { target->set_fill(std::move(fill)); });
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
    bind(font, context, [target](const Font& value) { target->set_font(value); });
    bind(content_, context, [target](const std::string& content) { target->set_content(content); });
    bind(style_.color ? *style_.color : Prop<Color>([environment] { return environment.get<ForegroundKey>(); }),
         context, [target](Color color) { target->set_color(color); });
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
    return std::make_unique<ButtonElement>(detail::lower(body, context.nested()), action_, pressed, hovered,
                                           enabled);
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
