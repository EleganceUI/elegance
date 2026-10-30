// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// View, its modifier sugar, the protocol base classes, and lowering.

#include "view/view_node.h"

#include "core/crash.h"
#include "core/timers.h"
#include "view/accessibility.h"
#include "view/text_field.h"
#include "view/window_drag.h"

#include <elg/animation.h>
#include <elg/error.h>
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

bool View::is_empty() const noexcept {
    return !node_;
}

View View::with_modifier(std::shared_ptr<const Modifier> modifier) const {
    return View(std::make_shared<const detail::ModifiedNode>(*this, std::move(modifier)));
}

View View::deferred(std::function<View()> body) {
    return View(std::make_shared<const detail::DeferredNode>(std::move(body)));
}

View View::padding(Prop<float> length) const {
    return padding(Edges::all, std::move(length));
}

View View::padding(Edges edges, Prop<float> length) const {
    const auto insets = [edges](float length) {
        const auto on = [&](Edges edge) { return contains(edges, edge) ? length : 0.f; };
        return EdgeInsets{on(Edges::top), on(Edges::leading), on(Edges::bottom), on(Edges::trailing)};
    };
    if (const float* value = length.static_value()) {
        return padding(insets(*value));
    }
    return padding(Prop<EdgeInsets>([length = std::move(length), insets](const EnvironmentValues& environment) {
        return insets(length.get(environment));
    }));
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

namespace {

// .frame(Prop<Size>): a frame whose size is bound to state.
class BoundFrameElement final : public detail::Element {
public:
    BoundFrameElement(Alignment alignment, std::unique_ptr<Element> content)
        : alignment_(alignment), content_(std::move(content)) {
        adopt(*content_);
    }

    void set_size(Size size) {
        if (size.width != size_.width || size.height != size_.height) {
            size_ = size;
            invalidate_layout();
        }
    }
    void inspect(detail::Inspection& out) const override { out.name = "Frame"; }
    float layout_priority() const noexcept override { return content_->layout_priority(); }
    float z_index() const noexcept override { return content_->z_index(); }
    void for_each_child(const std::function<void(Element&)>& visit) override { visit(*content_); }

protected:
    Size measure(ProposedSize) override { return size_; }
    void arrange(Rect bounds, ProposedSize) override {
        const ProposedSize offer{size_.width, size_.height};
        content_->place(aligned_position(bounds, content_->size_that_fits(offer), alignment_), offer);
    }
    void paint_impl(detail::DisplayList& list) const override { content_->paint(list); }
    bool hit_impl(Point point, std::vector<Element*>& path) override { return content_->hit_test(point, path); }

private:
    Alignment alignment_;
    Size size_;
    std::unique_ptr<Element> content_;
};

class BoundFrameNode final : public detail::ViewNode {
public:
    BoundFrameNode(View content, Prop<Size> size, Alignment alignment)
        : content_(std::move(content)), size_(std::move(size)), alignment_(alignment) {}
    std::unique_ptr<detail::Element> lower(const detail::LowerContext& context) const override {
        auto element = std::make_unique<BoundFrameElement>(alignment_, detail::lower(content_, context));
        BoundFrameElement* const target = element.get();
        detail::bind(size_, context, [target](Size size) { target->set_size(size); });
        return element;
    }

private:
    View content_;
    Prop<Size> size_;
    Alignment alignment_;
};

} // namespace

View View::frame(Prop<Size> size, Alignment alignment) const {
    return ViewAccess::make(std::make_shared<const BoundFrameNode>(*this, std::move(size), alignment));
}

namespace {

// .padding(Prop<EdgeInsets>): padding bound to state; lays out like PaddingModifier.
class BoundPaddingElement final : public detail::Element {
public:
    explicit BoundPaddingElement(std::unique_ptr<Element> content) : content_(std::move(content)) {
        adopt(*content_);
    }

    void set_insets(EdgeInsets insets) {
        if (insets.top != insets_.top || insets.leading != insets_.leading || insets.bottom != insets_.bottom ||
            insets.trailing != insets_.trailing) {
            insets_ = insets;
            invalidate_layout();
        }
    }
    void inspect(detail::Inspection& out) const override { out.name = "Padding"; }
    float layout_priority() const noexcept override { return content_->layout_priority(); }
    float z_index() const noexcept override { return content_->z_index(); }
    void for_each_child(const std::function<void(Element&)>& visit) override { visit(*content_); }

protected:
    Size measure(ProposedSize proposal) override {
        return PaddingModifier(insets_).size_that_fits(proposal, LayoutChild(*content_));
    }
    void arrange(Rect bounds, ProposedSize proposal) override {
        PaddingModifier(insets_).place(bounds, proposal, LayoutChild(*content_));
    }
    void paint_impl(detail::DisplayList& list) const override { content_->paint(list); }
    bool hit_impl(Point point, std::vector<Element*>& path) override { return content_->hit_test(point, path); }

private:
    EdgeInsets insets_;
    std::unique_ptr<Element> content_;
};

class BoundPaddingNode final : public detail::ViewNode {
public:
    BoundPaddingNode(View content, Prop<EdgeInsets> insets) : content_(std::move(content)), insets_(std::move(insets)) {}
    std::unique_ptr<detail::Element> lower(const detail::LowerContext& context) const override {
        auto element = std::make_unique<BoundPaddingElement>(detail::lower(content_, context));
        BoundPaddingElement* const target = element.get();
        detail::bind(insets_, context, [target](EdgeInsets insets) { target->set_insets(insets); });
        return element;
    }

private:
    View content_;
    Prop<EdgeInsets> insets_;
};

} // namespace

View View::padding(Prop<EdgeInsets> insets) const {
    if (const EdgeInsets* value = insets.static_value()) {
        return padding(*value);
    }
    return ViewAccess::make(std::make_shared<const BoundPaddingNode>(*this, std::move(insets)));
}

View View::fixed_size(bool horizontal, bool vertical) const {
    return modifier(FixedSizeModifier(horizontal, vertical));
}

View View::aspect_ratio(ContentMode mode) const {
    return modifier(AspectRatioModifier(std::nullopt, mode));
}

View View::aspect_ratio(float ratio, ContentMode mode) const {
    if (!(ratio > 0) || !std::isfinite(ratio)) {
        throw UsageError(std::format("aspect_ratio({}): the ratio must be positive", ratio));
    }
    return modifier(AspectRatioModifier(ratio, mode));
}

View View::layout_priority(float priority) const {
    return modifier(LayoutPriorityModifier(priority));
}

View View::z_index(Prop<float> z) const {
    return modifier(ZIndexModifier(std::move(z)));
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
    return clip_shape(Rectangle());
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

View View::inner_shadow(Shadow shadow) const {
    return modifier(InnerShadowModifier(shadow));
}

View View::blur(Prop<float> radius, bool opaque) const {
    return modifier(BlurModifier(std::move(radius), opaque));
}

View View::backdrop_blur(Prop<float> radius, BackdropOptions options) const {
    return backdrop_blur(std::move(radius), Rectangle(), std::move(options));
}

View View::backdrop_blur(Prop<float> radius, const Shape& shape, BackdropOptions options) const {
    const float* blur = radius.static_value();
    const float* saturation = options.saturation.static_value();
    const float* brightness = options.brightness.static_value();
    const float* noise = options.noise.static_value();
    if (blur && saturation && brightness && noise) {
        return background(detail::BackdropFill{*blur, *saturation, *brightness, *noise}, shape);
    }
    return background(Prop<ShapeStyle>([radius, options](const EnvironmentValues& environment) {
                          return ShapeStyle(detail::BackdropFill{
                              radius.get(environment), options.saturation.get(environment),
                              options.brightness.get(environment), options.noise.get(environment)});
                      }),
                      shape);
}

namespace {
ColorFilter color_filter(ColorFilter::Kind kind, Prop<float> amount) {
    ColorFilter filter;
    filter.kind = kind;
    filter.amount = std::move(amount);
    return filter;
}
} // namespace

View View::brightness(Prop<float> amount) const {
    return modifier(ColorFilterModifier(color_filter(ColorFilter::Kind::brightness, std::move(amount))));
}

View View::contrast(Prop<float> amount) const {
    return modifier(ColorFilterModifier(color_filter(ColorFilter::Kind::contrast, std::move(amount))));
}

View View::saturation(Prop<float> amount) const {
    return modifier(ColorFilterModifier(color_filter(ColorFilter::Kind::saturation, std::move(amount))));
}

View View::grayscale(Prop<float> amount) const {
    return modifier(ColorFilterModifier(color_filter(ColorFilter::Kind::grayscale, std::move(amount))));
}

View View::hue_rotation(Prop<float> degrees) const {
    return modifier(ColorFilterModifier(color_filter(ColorFilter::Kind::hue_rotation, std::move(degrees))));
}

View View::sepia(Prop<float> amount) const {
    return modifier(ColorFilterModifier(color_filter(ColorFilter::Kind::sepia, std::move(amount))));
}

View View::color_invert(Prop<float> amount) const {
    return modifier(ColorFilterModifier(color_filter(ColorFilter::Kind::invert, std::move(amount))));
}

View View::color_multiply(Prop<Color> color) const {
    ColorFilter filter;
    filter.kind = ColorFilter::Kind::multiply;
    filter.color = std::move(color);
    return modifier(ColorFilterModifier(std::move(filter)));
}

View View::luminance_to_alpha() const {
    return modifier(ColorFilterModifier(color_filter(ColorFilter::Kind::luminance_to_alpha, 1.f)));
}

View View::blend_mode(BlendMode mode) const {
    return modifier(BlendModeModifier(mode));
}

View View::compositing_group() const {
    return modifier(CompositingGroupModifier());
}

View View::rotation_effect(Prop<float> degrees, UnitPoint anchor) const {
    return modifier(RotationModifier(std::move(degrees), anchor));
}

View View::scale_effect(Prop<float> scale, UnitPoint anchor) const {
    return modifier(ScaleModifier(scale, scale, anchor));
}

View View::scale_effect(Prop<float> x, Prop<float> y, UnitPoint anchor) const {
    return modifier(ScaleModifier(std::move(x), std::move(y), anchor));
}

View View::mask(View mask, Alignment alignment) const {
    return modifier(MaskModifier(std::move(mask), alignment));
}

View View::mask(Prop<ShapeStyle> style) const {
    return mask(Rectangle().fill(std::move(style)));
}

View View::content_shape(const Shape& shape) const {
    return modifier(ContentShapeModifier(shape));
}

View View::allows_hit_testing(bool enabled) const {
    return modifier(AllowsHitTestingModifier(enabled));
}

View View::on_click(std::function<void(const ClickEvent&)> action, ClickOptions options) const {
    return modifier(ClickModifier(std::move(action), options));
}

View View::on_click(std::function<void()> action, ClickOptions options) const {
    return on_click([action = std::move(action)](const ClickEvent&) { action(); }, options);
}

View View::on_double_click(std::function<void(const ClickEvent&)> action) const {
    return on_click(std::move(action), {.count = 2});
}

View View::on_double_click(std::function<void()> action) const {
    return on_click(std::move(action), {.count = 2});
}

View View::on_activate(std::function<void()> action) const {
    if (!action) {
        throw UsageError("on_activate: the action is empty");
    }
    return View(std::make_shared<const detail::ActivateNode>(*this, std::move(action)));
}

View View::recognize(DragGesture gesture, GestureOptions options) const {
    return modifier(DragGestureModifier(std::move(gesture), options));
}

View View::recognize(LongPressGesture gesture, GestureOptions options) const {
    return modifier(LongPressGestureModifier(std::move(gesture), options));
}

View View::recognize(MagnifyGesture gesture) const {
    return modifier(MagnifyGestureModifier(std::move(gesture)));
}

View View::recognize(RotateGesture gesture) const {
    return modifier(RotateGestureModifier(std::move(gesture)));
}

View View::recognize(TwoFingerDoubleTapGesture gesture) const {
    return modifier(TwoFingerDoubleTapModifier(std::move(gesture)));
}

View View::on_drag(std::function<void(const DragEvent&)> action) const {
    return recognize(DragGesture().on_changed(std::move(action)));
}

View View::on_long_press(std::function<void()> action) const {
    return recognize(LongPressGesture(std::move(action)));
}

View View::on_magnify(std::function<void(const MagnifyEvent&)> action) const {
    return recognize(MagnifyGesture().on_changed(std::move(action)));
}

View View::on_rotate(std::function<void(const RotateEvent&)> action) const {
    return recognize(RotateGesture().on_changed(std::move(action)));
}

View View::on_two_finger_double_tap(std::function<void(Point)> action) const {
    return recognize(TwoFingerDoubleTapGesture(std::move(action)));
}

View View::pointer_style(Prop<PointerStyle> style) const {
    return modifier(PointerStyleModifier(std::move(style)));
}

View View::on_size_change(std::function<void(PixelSize)> action) const {
    struct Observer final : EffectModifier {
        std::function<void(PixelSize)> action;
        explicit Observer(std::function<void(PixelSize)> callback) : action(std::move(callback)) {}
        void apply(Effects& effects) const override { effects.on_size_change = action; }
    };
    return modifier(Observer(std::move(action)));
}

View View::on_pointer_enter(std::function<void()> action) const {
    return modifier(PointerBoundaryModifier(std::move(action), nullptr));
}

View View::on_pointer_leave(std::function<void()> action) const {
    return modifier(PointerBoundaryModifier(nullptr, std::move(action)));
}

View View::on_hover_change(std::function<void(bool)> action) const {
    return modifier(HoverModifier(std::move(action)));
}

View View::hover_delay(HoverDelay delay) const {
    return modifier(HoverDelayModifier(delay));
}

namespace {

// on_appear and on_disappear: tied to the scope the view is built in (its If/ForEach item, its
// lazy stack row, its window), which ends when the view is gone.
class LifecycleNode final : public detail::ViewNode {
public:
    LifecycleNode(View content, std::function<void()> appear, std::function<void()> disappear)
        : content_(std::move(content)), appear_(std::move(appear)), disappear_(std::move(disappear)) {}

    std::unique_ptr<detail::Element> lower(const detail::LowerContext& context) const override {
        auto element = detail::lower(content_, context);
        struct Life {
            bool appeared = false;
            bool gone = false;
        };
        auto life = std::make_shared<Life>();
        detail::Scope* scope = detail::current_scope();
        detail::queue_lifecycle_action([life, scope, appear = appear_] {
            if (life->gone) {
                return; // removed before it got to appear
            }
            life->appeared = true;
            if (appear) {
                std::optional<detail::Scope::Use> use;
                if (scope) {
                    use.emplace(*scope);
                }
                appear();
            }
        });
        if (scope) {
            scope->on_end([life, disappear = disappear_] {
                life->gone = true;
                if (life->appeared && disappear) {
                    detail::queue_lifecycle_action(disappear);
                }
            });
        }
        return element;
    }

private:
    View content_;
    std::function<void()> appear_;
    std::function<void()> disappear_;
};

} // namespace

View View::on_appear(std::function<void()> action) const {
    return View(std::make_shared<const LifecycleNode>(*this, std::move(action), nullptr));
}

View View::on_disappear(std::function<void()> action) const {
    return View(std::make_shared<const LifecycleNode>(*this, nullptr, std::move(action)));
}

namespace {

class WindowDragAreaModifier final : public EventModifier {
public:
    explicit WindowDragAreaModifier(bool enabled) noexcept : enabled_(enabled) {}
    std::unique_ptr<EventHandler> make_handler() const override {
        return std::make_unique<detail::WindowDragHandler>(enabled_);
    }

private:
    bool enabled_;
};

} // namespace

View View::window_drag_area(bool enabled) const {
    return modifier(WindowDragAreaModifier(enabled));
}

View View::focusable(bool focusable) const {
    return modifier(FocusableModifier(focusable));
}

View View::focused(Binding<bool> is_focused) const {
    return View(std::make_shared<const detail::FocusedNode>(*this, std::move(is_focused)));
}

View View::on_key_press(Key key, std::function<void()> action) const {
    return on_key([key, action = std::move(action)](const KeyEvent& event) {
        if (event.kind != KeyEvent::Kind::down || event.key != key) {
            return false;
        }
        if (action) {
            action();
        }
        return true;
    });
}

View View::on_key(std::function<bool(const KeyEvent&)> action) const {
    return modifier(KeyEventModifier(std::move(action)));
}

View View::on_text_input(std::function<bool(const TextInputEvent&)> action) const {
    return modifier(TextInputModifier(std::move(action)));
}

View View::on_pointer(std::function<bool(const PointerEvent&)> action) const {
    return modifier(PointerModifier(std::move(action)));
}

View View::on_scroll(std::function<bool(const ScrollEvent&)> action) const {
    return modifier(ScrollModifier(std::move(action)));
}

View View::tint(Prop<Color> color) const {
    return modifier(TintModifier(std::move(color)));
}

View View::overlay(View view, Alignment alignment) const {
    return modifier(OverlayModifier(std::move(view), alignment));
}

View View::border(Prop<Color> color, float width) const {
    return border(std::move(color), width, Rectangle());
}

View View::border(Prop<Color> color, float width, const Shape& shape) const {
    return overlay(shape.stroke_border(std::move(color), width));
}

View View::font(Prop<Font> font) const {
    return modifier(FontModifier(std::move(font)));
}

View View::font_weight(Prop<Weight> weight) const {
    // Resolved against the surrounding environment, so this reads the font from outside.
    return font([weight = std::move(weight)](const EnvironmentValues& environment) {
        return environment.get<FontKey>().weight(weight.get(environment));
    });
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

View View::checkbox_style_impl(std::shared_ptr<const CheckboxStyle> style) const {
    return modifier(CheckboxStyleModifier(std::move(style)));
}

View View::number_input_style_impl(std::shared_ptr<const NumberInputStyle> style) const {
    return modifier(NumberInputStyleModifier(std::move(style)));
}

View View::progress_style_impl(std::shared_ptr<const ProgressStyle> style) const {
    return modifier(ProgressStyleModifier(std::move(style)));
}

View View::picker_style_impl(std::shared_ptr<const PickerStyle> style) const {
    return modifier(PickerStyleModifier(std::move(style)));
}

View View::slider_style_impl(std::shared_ptr<const SliderStyle> style) const {
    return modifier(SliderStyleModifier(std::move(style)));
}

View View::popover_style_impl(std::shared_ptr<const PopoverStyle> style) const {
    return modifier(PopoverStyleModifier(std::move(style)));
}

View View::tooltip_style_impl(std::shared_ptr<const TooltipStyle> style) const {
    return modifier(TooltipStyleModifier(std::move(style)));
}

View View::menu_style_impl(std::shared_ptr<const MenuStyle> style) const {
    return modifier(MenuStyleModifier(std::move(style)));
}

View View::row_style_impl(std::shared_ptr<const RowStyle> style) const {
    return modifier(RowStyleModifier(std::move(style)));
}

View View::navigation_stack_style_impl(std::shared_ptr<const NavigationStackStyle> style) const {
    return modifier(NavigationStackStyleModifier(std::move(style)));
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

const void* LayoutChild::layout_value(const std::type_info& key) const {
    return element_->find_layout_value(key);
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

View Button(Prop<std::string> title, std::function<void()> action) {
    return Button(Text(std::move(title)), std::move(action));
}

View TextField(Prop<std::string> placeholder, Binding<std::string> text, TextFieldOptions options) {
    const bool* secure = options.secure.static_value();
    const int* lines = options.line_limit.static_value();
    if (secure && *secure && lines && *lines > 1) {
        throw UsageError("TextField: a secure field is one line (line_limit must be 1)");
    }
    return ViewAccess::make(std::make_shared<const detail::TextFieldNode>(std::move(placeholder), std::move(text),
                                                                         std::move(options), false));
}

View TextArea(Prop<std::string> placeholder, Binding<std::string> text, TextAreaOptions options) {
    TextFieldOptions field{.on_submit = std::move(options.on_submit),
                           .styler = std::move(options.styler),
                           .on_key = std::move(options.on_key)};
    return ViewAccess::make(std::make_shared<const detail::TextFieldNode>(std::move(placeholder), std::move(text),
                                                                         std::move(field), true));
}

ScrollPosition::ScrollPosition() : target_(std::make_shared<detail::ScrollTarget>()) {}

namespace {

void request_scroll(detail::ScrollTarget& target, detail::ScrollRequest request) {
    target.request = std::move(request);
    if (target.needs_frame) {
        *target.needs_frame = true; // applied in the scroll view's next layout
    }
}

} // namespace

void ScrollPosition::scroll_to_top(bool animated) const {
    request_scroll(*target_, {.kind = detail::ScrollRequest::Kind::top, .animated = animated});
}

void ScrollPosition::scroll_to_end(bool animated) const {
    request_scroll(*target_, {.kind = detail::ScrollRequest::Kind::end, .animated = animated});
}

void ScrollPosition::scroll_to_key(ItemKey key) const {
    request_scroll(*target_, {.kind = detail::ScrollRequest::Kind::item, .animated = false, .key = std::move(key), .settle = 4});
}

View ScrollView(View content) {
    return ScrollView({}, std::move(content));
}

View ScrollView(ScrollViewOptions options, View content) {
    return ViewAccess::make(std::make_shared<const detail::ScrollNode>(options, std::move(content)));
}

View Toggle(View label, Binding<bool> is_on) {
    return ViewAccess::make(
        std::make_shared<const detail::ToggleNode>(detail::ToggleNode::Kind::toggle, std::move(label), std::move(is_on)));
}

View Toggle(Prop<std::string> title, Binding<bool> is_on) {
    return Toggle(Text(std::move(title)), std::move(is_on));
}

View Toggle(Binding<bool> is_on) {
    return Toggle(EmptyView(), std::move(is_on));
}

View Checkbox(Binding<bool> is_checked, CheckboxOptions options) {
    return Checkbox(EmptyView(), std::move(is_checked), std::move(options));
}

View Checkbox(View label, Binding<bool> is_checked, CheckboxOptions options) {
    return ViewAccess::make(std::make_shared<const detail::ToggleNode>(
        detail::ToggleNode::Kind::checkbox, std::move(label), std::move(is_checked), std::move(options.mixed)));
}

View Checkbox(Prop<std::string> title, Binding<bool> is_checked, CheckboxOptions options) {
    return Checkbox(Text(std::move(title)), std::move(is_checked), std::move(options));
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

// A .disabled() view's handlers stop acting (DisabledEventHandler); the dispatcher cancels what
// they had going (release_disabled). A window drag area still moves the window.
std::unique_ptr<EventElement> make_event_element(std::unique_ptr<EventHandler> handler, const View& content,
                                                 const LowerContext& context) {
    const bool drags_window = dynamic_cast<const WindowDragHandler*>(handler.get()) != nullptr;
    auto element = std::make_unique<EventElement>(std::move(handler), detail::lower(content, context),
                                                  environment_hover_delay(context.environment));
    join_focus(*element, context);
    if (!drags_window) {
        const EnvironmentValues environment = context.environment;
        detail::bind(Prop<bool>([environment] { return !environment.get<DisabledKey>(); }), context,
                     [target = element.get()](bool enabled) { target->set_enabled(enabled); });
    }
    return element;
}

// on_activate: a left click inside, or Space/Enter while focused, while enabled.
class ActivateHandler final : public EventHandler {
public:
    ActivateHandler(std::function<void()> action, std::function<bool()> enabled)
        : action_(std::move(action)), enabled_(std::move(enabled)) {}

    EventResult pointer(const PointerEvent& event, Rect frame) override {
        if (event.kind == PointerEvent::Kind::cancel) {
            pressed_ = false;
            return {};
        }
        if (event.button != MouseButton::left) {
            return {};
        }
        switch (event.kind) {
        case PointerEvent::Kind::down:
            if (!untrack(enabled_)) {
                return {};
            }
            pressed_ = true;
            return {.consumed = true, .cancellable = true};
        case PointerEvent::Kind::up: {
            const bool inside = event.location.x >= frame.x && event.location.x < frame.max_x() &&
                                event.location.y >= frame.y && event.location.y < frame.max_y();
            if (std::exchange(pressed_, false) && inside && untrack(enabled_)) {
                action_();
            }
            return {.consumed = true};
        }
        default: return {};
        }
    }
    EventResult key(const KeyEvent& event) override {
        if (event.kind != KeyEvent::Kind::down || (event.key != Key::space && event.key != Key::enter) ||
            event.modifiers != KeyModifiers::none || !untrack(enabled_)) {
            return {};
        }
        if (!event.repeat) {
            action_();
        }
        return {.consumed = true};
    }
    bool focusable() const override { return untrack(enabled_); }

private:
    std::function<void()> action_;
    std::function<bool()> enabled_;
    bool pressed_ = false;
};

// `.focused(binding)` around a view: focusable itself only when nothing inside is (so Tab and
// clicks reach the field it wraps, not an invisible wrapper first).
class FocusScopeHandler final : public EventHandler {
public:
    void set_scope(Element* scope) noexcept { scope_ = scope; }
    bool focusable() const override {
        bool inner = false;
        std::function<void(Element&)> walk = [&](Element& element) {
            if (inner) {
                return;
            }
            if (EventHandler* handler = element.event_handler(); handler && handler->focusable()) {
                inner = true;
                return;
            }
            element.for_each_child(walk);
        };
        scope_->for_each_child(walk);
        return !inner;
    }

private:
    Element* scope_ = nullptr;
};

} // namespace

// Focusable elements draw the ring in the tint.
void join_focus(Element& element, const LowerContext& context) {
    if (context.focus) {
        element.set_focus_manager(context.focus,
                                  [environment = context.environment] { return environment.get<TintKey>(); });
    }
}

std::unique_ptr<ButtonElement> make_button_element(const View& body, std::function<void()> action, State<bool> pressed,
                                                   State<bool> hovered, const LowerContext& context) {
    const EnvironmentValues environment = context.environment;
    auto element = std::make_unique<ButtonElement>(
        detail::lower(body, context.nested()), std::move(action), std::move(pressed), std::move(hovered),
        [environment] { return !environment.get<DisabledKey>(); });
    join_focus(*element, context);
    return element;
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
        const Prop<float> offset_x = effects.offset_x;
        const Prop<float> offset_y = effects.offset_y;
        const std::optional<Prop<float>> z_index = effects.z_index;
        const std::optional<Prop<float>> blur = effects.blur;
        const std::optional<ColorFilter> filter = effects.color_filter;
        const std::optional<Prop<float>> rotation = effects.rotation;
        const std::optional<Prop<float>> scale_x = effects.scale_x;
        const std::optional<Prop<float>> scale_y = effects.scale_y;
        auto element = std::make_unique<EffectElement>(std::move(effects), detail::lower(content_, context));
        EffectElement* const target = element.get();
        if (blur) {
            detail::bind(*blur, context, [target](float value, const Animation* animation) { target->set_blur(value, animation); });
        }
        if (filter) {
            detail::bind(filter->amount, context,
                         [target](float value, const Animation* animation) { target->set_filter_amount(value, animation); });
            detail::bind(filter->color, context,
                         [target](Color value, const Animation* animation) { target->set_filter_color(value, animation); });
        }
        if (rotation) {
            detail::bind(*rotation, context,
                         [target](float value, const Animation* animation) { target->set_rotation(value, animation); });
        }
        if (scale_x) {
            detail::bind(*scale_x, context,
                         [target](float value, const Animation* animation) { target->set_scale_x(value, animation); });
        }
        if (scale_y) {
            detail::bind(*scale_y, context,
                         [target](float value, const Animation* animation) { target->set_scale_y(value, animation); });
        }
        detail::bind(opacity, context, [target](float value, const Animation* animation) { target->set_opacity(value, animation); });
        detail::bind(offset_x, context, [target](float value, const Animation* animation) { target->set_offset_x(value, animation); });
        detail::bind(offset_y, context, [target](float value, const Animation* animation) { target->set_offset_y(value, animation); });
        if (z_index) {
            detail::bind(*z_index, context, [target](float value) { target->set_z_index(value); });
        }
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
    // A selection around a stack over a ForEach is the ForEach's; its other children (a header)
    // aren't rows, and nothing inside them can take it.
    const auto is_for_each = [](const View& child) {
        return dynamic_cast<const ForEachNode*>(ViewAccess::node(child).get()) != nullptr;
    };
    LowerContext other_context = child_context;
    if (context.selection && std::ranges::any_of(children_, is_for_each)) {
        other_context.selection.reset();
    }
    std::vector<std::unique_ptr<Element>> children;
    children.reserve(children_.size());
    for (const View& child : children_) {
        children.push_back(detail::lower(child, is_for_each(child) ? child_context : other_context));
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
    Prop<Font> font = style_.font ? style_.font->resolved_in(environment)
                                  : Prop<Font>([environment] { return environment.get<FontKey>(); });
    if (style_.weight) {
        font = Prop<Font>([font, weight = style_.weight->resolved_in(environment)] {
            return font.get().weight(weight.get());
        });
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
    const State<bool> focused(false);
    const EnvironmentValues environment = context.environment;
    const auto enabled = [environment] { return !environment.get<DisabledKey>(); };
    const ButtonConfiguration configuration{
        .label = label_,
        .is_pressed = [pressed] { return pressed(); },
        .is_hovered = [hovered] { return hovered(); },
        .is_enabled = enabled,
        .is_focused = [focused] { return focused(); },
    };

    // The style is chosen when the button is built.
    const std::shared_ptr<const ButtonStyle> style = untrack([&] { return environment.get<ButtonStyleKey>(); });
    View body;
    {
        const EnvironmentScope scope(environment); // Environment<K>() in make_body
        body = style->make_body(configuration);
    }
    auto element = make_button_element(body, action_, pressed, hovered, context);
    element->set_focus_state(focused);
    return element;
}

namespace {

// Where a TextField's style puts the text.
class TextEditorNode final : public ViewNode {
public:
    explicit TextEditorNode(std::shared_ptr<TextEditor> editor) : editor_(std::move(editor)) {}
    // The text takes its font and colors from where the style put it (so a style's .font() or
    // .foreground_style() on the field applies).
    std::unique_ptr<Element> lower(const LowerContext& context) const override {
        const EnvironmentValues environment = context.environment;
        editor_->set_environment(environment);
        auto element = std::make_unique<TextEditorElement>(editor_);
        detail::bind(Prop<Font>([environment] { return environment.get<FontKey>(); }), context,
                     [editor = editor_](const Font& font) { editor->set_font(font); });
        return element;
    }

private:
    std::shared_ptr<TextEditor> editor_;
};

} // namespace

std::unique_ptr<Element> TextFieldNode::lower(const LowerContext& context) const {
    ELG_ASSERT(context.text, "lowering a TextField without a text system");
    const EnvironmentValues environment = context.environment;
    const auto editor = std::make_shared<TextEditor>(*context.text, text_, options_.on_submit, context.services,
                                                     context.focus, environment, area_);
    editor->set_styler(options_.styler);
    detail::bind(placeholder_, context, [editor](std::string placeholder) { editor->set_placeholder(std::move(placeholder)); });
    if (!area_) {
        // A field revealed or hidden while focused switches its text input (IME) with it.
        detail::bind(options_.secure, context, [editor, focus = context.focus](bool secure) {
            editor->set_secure(secure);
            if (focus) {
                focus->refresh_text_input();
            }
        });
        detail::bind(options_.line_limit, context, [editor](int lines) { editor->set_line_limit(lines); });
    }
    // The text follows the binding (when changed elsewhere). The font is bound where the style
    // places the field (TextEditorNode).
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
                                                      enabled);
    if (context.focus) {
        element->set_focus_manager(context.focus, {}); // the style shows focus
    }
    if (options_.on_key) {
        element->set_key_filter(options_.on_key);
    }
    return element;
}

std::unique_ptr<Element> ScrollNode::lower(const LowerContext& context) const {
    const EnvironmentValues environment = context.environment;
    auto element = std::make_unique<ScrollElement>(options_, detail::lower(content_, context.nested()),
                                                   [environment] { return environment.color(ThemeColor::secondary); },
                                                   context.needs_frame);
    ScrollElement* const target = element.get();
    detail::bind(options_.shows_indicators, context, [target](bool shows) { target->set_shows_indicators(shows); });
    detail::bind(options_.sticks_to_end, context, [target](bool sticks) { target->set_sticks_to_end(sticks); });
    return element;
}

std::unique_ptr<Element> ToggleNode::lower(const LowerContext& context) const {
    // A button whose action flips the binding (and checks a mixed check box); the style draws it.
    const State<bool> pressed(false);
    const State<bool> hovered(false);
    const State<bool> focused(false);
    const EnvironmentValues environment = context.environment;
    const Binding<bool> value = value_;
    const Prop<bool> mixed = mixed_;
    const bool checkbox = kind_ == Kind::checkbox;

    // The style is chosen when the control is built, and with it how a flip animates.
    std::shared_ptr<const ToggleStyle> toggle_style;
    std::shared_ptr<const CheckboxStyle> checkbox_style;
    std::optional<Animation> animation;
    if (checkbox) {
        checkbox_style = untrack([&] { return environment.get<CheckboxStyleKey>(); });
        animation = checkbox_style->animation();
    } else {
        toggle_style = untrack([&] { return environment.get<ToggleStyleKey>(); });
        animation = toggle_style->animation();
    }
    const auto toggle = [value, mixed, environment, animation] {
        const bool next = untrack([&] { return mixed.get(environment) || !value(); });
        if (animation) {
            with_animation(*animation, [&] { value.set(next); });
        } else {
            value.set(next);
        }
    };

    View body;
    {
        const EnvironmentScope scope(environment);
        const Signal<bool> is_enabled = [environment] { return !environment.get<DisabledKey>(); };
        if (checkbox) {
            body = checkbox_style->make_body({
                .label = label_,
                .is_checked = value,
                .is_mixed = [mixed, environment] { return mixed.get(environment); },
                .toggle = toggle,
                .is_pressed = pressed,
                .is_hovered = hovered,
                .is_enabled = is_enabled,
                .is_focused = focused,
            });
        } else {
            body = toggle_style->make_body({
                .label = label_,
                .is_on = value,
                .toggle = toggle,
                .is_pressed = pressed,
                .is_hovered = hovered,
                .is_enabled = is_enabled,
                .is_focused = focused,
            });
        }
    }
    AccessibilitySpec semantics;
    semantics.role = checkbox ? AccessRole::checkbox : AccessRole::toggle;
    semantics.toggled = [value, mixed, environment]() -> std::optional<ToggleState> {
        if (mixed.get(environment)) {
            return ToggleState::mixed;
        }
        return value() ? ToggleState::on : ToggleState::off;
    };
    auto element = make_button_element(body, toggle, pressed, hovered, context);
    element->set_focus_state(focused);
    return with_accessibility(std::move(element), std::move(semantics));
}

// Like SwiftUI: the binding is true while focus is anywhere inside the view, and setting it
// focuses the first focusable view inside (or the view itself if nothing inside can be).
std::unique_ptr<Element> ActivateNode::lower(const LowerContext& context) const {
    const EnvironmentValues environment = context.environment;
    const std::function<bool()> enabled = [environment] { return !environment.get<DisabledKey>(); };
    const std::function<void()> action = action_;
    AccessibilitySpec semantics;
    semantics.action = [action, enabled] {
        if (untrack(enabled)) {
            action();
        }
    };
    auto element = make_event_element(std::make_unique<ActivateHandler>(action, enabled), content_, context);
    return with_accessibility(std::move(element), std::move(semantics), environment);
}

std::unique_ptr<Element> FocusedNode::lower(const LowerContext& context) const {
    auto handler = std::make_unique<FocusScopeHandler>();
    FocusScopeHandler* const scope_handler = handler.get();
    auto element = make_event_element(std::move(handler), content_, context);
    scope_handler->set_scope(element.get());
    if (!context.focus) {
        return element;
    }
    // Focus -> binding.
    context.focus->watch(element.get(), [binding = is_focused_](bool within) { binding.set(within); });
    // Binding -> focus. The focus manager updates before notifying, so the binding write a focus
    // change causes finds nothing to do here.
    effect([binding = is_focused_, scope = element.get(), focus = context.focus, needs_frame = context.needs_frame] {
        const bool wanted = binding();
        const bool within = focus->contains_focus(scope);
        if (wanted == within) {
            return;
        }
        if (!wanted) {
            focus->focus(nullptr, false);
        } else if (Element* target = FocusManager::first_focusable(*scope)) {
            focus->focus(target, true);
        } else {
            return;
        }
        if (needs_frame) {
            *needs_frame = true;
        }
    });
    return element;
}

std::unique_ptr<Element> SpacerNode::lower(const LowerContext& context) const {
    return std::make_unique<SpacerElement>(min_length_, context.spacer_axis);
}

std::unique_ptr<Element> CanvasNode::lower(const LowerContext& context) const {
    auto element = std::make_unique<CanvasElement>(draw_);
    // The canvas redraws when state its drawing reads changes: an effect runs the drawing once
    // (into nothing) to see what it reads, and asks for a frame whenever that changes.
    CanvasElement* const target = element.get();
    effect([draw = draw_, target, needs_frame = context.needs_frame, first = std::make_shared<bool>(true)] {
        if (!draw) {
            return;
        }
        DisplayList scratch;
        GraphicsContext graphics(scratch, target->frame(), 1);
        draw(graphics);
        if (!std::exchange(*first, false) && needs_frame) {
            *needs_frame = true;
        }
    });
    return element;
}

std::unique_ptr<Element> DeferredNode::lower(const LowerContext& context) const {
    return detail::lower(body_(), context);
}

} // namespace elg::detail
