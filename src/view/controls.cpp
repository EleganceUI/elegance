// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Slider (an element that places its SliderStyle's parts), Picker (a PickerStyle's body), and
// their default styles, composed from the public API like an app's own styles would be.

#include "view/view_node.h"

#include <elg/animation.h>
#include <elg/shapes.h>
#include <elg/styles.h>
#include <elg/text.h>
#include <elg/views.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace elg::detail {

namespace {

bool contains(const Rect& rect, Point point) {
    return point.x >= rect.x && point.y >= rect.y && point.x < rect.max_x() && point.y < rect.max_y();
}

constexpr float kDefaultWidth = 160;

// --- Slider ---

// The SliderStyle's track, fill and thumb, placed by the value. A new value moves them through
// layout, so an animated change eases them like any other layout change.
struct SliderParts {
    std::unique_ptr<Element> track;
    std::unique_ptr<Element> fill;
    std::unique_ptr<Element> thumb;
};

// What the style's configuration reads, written by the slider.
struct SliderState {
    State<float> fraction{0.f};
    State<bool> pressed{false};
    State<bool> hovered{false};
    State<bool> focused{false};
};

class SliderElement final : public Element, private EventHandler {
public:
    SliderElement(Binding<float> value, SliderOptions options, EnvironmentValues environment, SliderState state,
                  SliderParts parts)
        : value_(std::move(value)), options_(std::move(options)), environment_(std::move(environment)),
          state_(std::move(state)), parts_(std::move(parts)) {
        adopt(*parts_.track);
        adopt(*parts_.fill);
        adopt(*parts_.thumb);
    }

    void set_value(float value) { show(fraction(value)); }
    // SliderOptions' min, max, and step; the thumb moves to where the value is in the new range.
    void set_range(float min, float max, float step) {
        min_ = min;
        max_ = max > min ? max : min + 1;
        step_ = step;
        show(fraction(untrack([&] { return value_(); })));
    }

    EventHandler* event_handler() noexcept override { return this; }
    void for_each_child(const std::function<void(Element&)>& visit) override {
        visit(*parts_.track);
        visit(*parts_.fill);
        visit(*parts_.thumb);
    }

    void accessibility(AccessibilityInfo& out) const override {
        out.role = AccessRole::slider;
        out.numeric = untrack([&] { return value_(); });
        out.minimum = min_;
        out.maximum = max_;
        out.step = adjust_step();
        out.disabled = !enabled();
        out.can_adjust = out.can_set_value = out.can_focus = !out.disabled;
    }
    bool accessibility_action(const AccessibilityAction& action) override {
        if (!enabled()) {
            return false;
        }
        const float now = untrack([&] { return value_(); });
        switch (action.kind) {
        case AccessibilityAction::Kind::increment: write(now + adjust_step(), nullptr); return true;
        case AccessibilityAction::Kind::decrement: write(now - adjust_step(), nullptr); return true;
        case AccessibilityAction::Kind::set_numeric: write(static_cast<float>(action.number), nullptr); return true;
        default: return false;
        }
    }

protected:
    Size measure(ProposedSize proposal) override {
        float width = proposal.width.value_or(kDefaultWidth);
        if (!std::isfinite(width)) {
            width = kDefaultWidth;
        }
        const Size thumb = parts_.thumb->size_that_fits({});
        const Size track = parts_.track->size_that_fits({std::max(width - thumb.width, 0.f), std::nullopt});
        return {width, std::max(thumb.height, track.height)};
    }
    void arrange(Rect bounds, ProposedSize) override {
        const Size thumb = parts_.thumb->size_that_fits({});
        thumb_width_ = thumb.width;
        const float center_y = bounds.y + bounds.height / 2;
        const float start = bounds.x + thumb.width / 2;
        const ProposedSize along{track_length(), std::nullopt};
        const Size track = parts_.track->size_that_fits(along);
        parts_.track->place({start, center_y - track.height / 2}, along);
        const float thumb_x = start + fraction_ * track_length();
        const ProposedSize filled{thumb_x - start, track.height};
        const Size fill = parts_.fill->size_that_fits(filled);
        parts_.fill->place({start, center_y - fill.height / 2}, filled);
        parts_.thumb->place({thumb_x - thumb.width / 2, center_y - thumb.height / 2}, {});
    }
    void paint_impl(DisplayList& list) const override {
        parts_.track->paint(list);
        parts_.fill->paint(list);
        parts_.thumb->paint(list);
    }
    // The whole slider takes the pointer; its parts only draw.
    bool hit_impl(Point point, std::vector<Element*>&) override { return contains(frame(), point); }

private:
    float fraction(float value) const {
        return std::clamp((value - min_) / (max_ - min_), 0.f, 1.f);
    }
    void show(float fraction) {
        fraction_ = fraction;
        state_.fraction = fraction;
        invalidate_layout();
    }
    // What a screen reader's increment moves: the step, else a twentieth of the range.
    float adjust_step() const { return step_ > 0 ? step_ : (max_ - min_) / 20; }
    bool enabled() const {
        return !untrack([&] { return environment_.get<DisabledKey>(); });
    }
    // The thumb's center travels the slider's width less the thumb's.
    float track_start() const { return frame().x + thumb_width_ / 2; }
    float track_length() const { return std::max(frame().width - thumb_width_, 1.f); }

    // Writes `value`, snapped to the step and clamped to the range.
    void write(float value, const Animation* animation) {
        if (step_ > 0) {
            value = min_ + std::round((value - min_) / step_) * step_;
        }
        value = std::clamp(value, min_, max_);
        if (animation) {
            with_animation(*animation, [&] { value_.set(value); });
        } else {
            value_.set(value);
        }
    }
    void write_at(float x) {
        const float f = std::clamp((x - track_start()) / track_length(), 0.f, 1.f);
        write(min_ + f * (max_ - min_), nullptr);
    }

    EventResult pointer(const PointerEvent& event, Rect) override {
        if (event.button != MouseButton::left && event.kind != PointerEvent::Kind::cancel) {
            return {};
        }
        switch (event.kind) {
        case PointerEvent::Kind::hover: return {};
        case PointerEvent::Kind::down:
            if (!enabled()) {
                return {.consumed = true};
            }
            dragging_ = true;
            state_.pressed = true;
            if (options_.on_editing_changed) {
                options_.on_editing_changed(true);
            }
            write_at(event.location.x);
            return {.consumed = true, .redraw = true};
        case PointerEvent::Kind::move:
            if (dragging_) {
                write_at(event.location.x);
                return {.consumed = true, .redraw = true};
            }
            return {};
        case PointerEvent::Kind::up:
        case PointerEvent::Kind::cancel:
            state_.pressed = false;
            if (std::exchange(dragging_, false) && options_.on_editing_changed) {
                options_.on_editing_changed(false);
            }
            return {.consumed = true, .redraw = true};
        }
        return {};
    }

    EventResult hover(bool inside) override {
        state_.hovered = inside;
        return {.redraw = true};
    }

    EventResult focus(bool focused) override {
        state_.focused = focused;
        return {.redraw = true};
    }

    EventResult key(const KeyEvent& event) override {
        if (event.kind != KeyEvent::Kind::down || !enabled()) {
            return {};
        }
        const float range = max_ - min_;
        const float increment = step_ > 0 ? step_ : range / 20;
        const float page = std::max(increment, range / 10);
        const float current = untrack([&] { return value_(); });
        float target;
        switch (event.key) {
        case Key::left:
        case Key::down: target = current - increment; break;
        case Key::right:
        case Key::up: target = current + increment; break;
        case Key::page_down: target = current - page; break;
        case Key::page_up: target = current + page; break;
        case Key::home: target = min_; break;
        case Key::end: target = max_; break;
        default: return {};
        }
        static const Animation animation = Animation::ease_out(0.12);
        write(target, &animation);
        return {.consumed = true, .redraw = true};
    }

    bool focusable() const override { return enabled(); }

    Binding<float> value_;
    SliderOptions options_;
    float min_ = 0;
    float max_ = 1;
    float step_ = 0;
    EnvironmentValues environment_;
    SliderState state_;
    SliderParts parts_;
    float fraction_ = 0;    // where the value is, 0...1
    float thumb_width_ = 0; // from the last arrange
    bool dragging_ = false;
};

class SliderNode final : public ViewNode {
public:
    SliderNode(Binding<float> value, SliderOptions options) : value_(std::move(value)), options_(std::move(options)) {}
    std::unique_ptr<Element> lower(const LowerContext& context) const override {
        const EnvironmentValues environment = context.environment;
        const SliderState state;
        const SliderConfiguration configuration{
            .fraction = [fraction = state.fraction] { return fraction(); },
            .is_pressed = [pressed = state.pressed] { return pressed(); },
            .is_hovered = [hovered = state.hovered] { return hovered(); },
            .is_enabled = [environment] { return !environment.get<DisabledKey>(); },
            .is_focused = [focused = state.focused] { return focused(); },
        };
        const std::shared_ptr<const SliderStyle> style = untrack([&] { return environment.get<SliderStyleKey>(); });
        View track;
        View fill;
        View thumb;
        {
            const EnvironmentScope scope(environment);
            track = style->make_track(configuration);
            fill = style->make_fill(configuration);
            thumb = style->make_thumb(configuration);
        }
        const LowerContext nested = context.nested();
        auto element = std::make_unique<SliderElement>(
            value_, options_, environment, state,
            SliderParts{detail::lower(track, nested), detail::lower(fill, nested), detail::lower(thumb, nested)});
        SliderElement* const target = element.get();
        // The range first, so the value's first fraction is in it.
        const float* min = options_.min.static_value();
        const float* max = options_.max.static_value();
        const float* step = options_.step.static_value();
        if (min && max && step) {
            target->set_range(*min, *max, *step);
        } else {
            const Prop<std::array<float, 3>> range([options = options_](const EnvironmentValues& env) {
                return std::array{options.min.get(env), options.max.get(env), options.step.get(env)};
            });
            detail::bind(range, context, [target](std::array<float, 3> r) { target->set_range(r[0], r[1], r[2]); });
        }
        detail::bind(Prop<float>(value_), context, [target](float value) { target->set_value(value); });
        if (context.focus) {
            element->set_focus_manager(context.focus,
                                       [environment = context.environment] { return environment.get<TintKey>(); });
        }
        return element;
    }

private:
    Binding<float> value_;
    SliderOptions options_;
};

// --- Picker ---

class PickerNode final : public ViewNode {
public:
    PickerNode(std::vector<std::string> labels, std::function<std::size_t()> selected,
               std::function<void(std::size_t)> select)
        : labels_(std::move(labels)), selected_(std::move(selected)), select_(std::move(select)) {}

    std::unique_ptr<Element> lower(const LowerContext& context) const override {
        const EnvironmentValues& environment = context.environment;
        const std::shared_ptr<const PickerStyle> style = untrack([&] { return environment.get<PickerStyleKey>(); });
        View body;
        {
            const EnvironmentScope scope(environment);
            body = style->make_body({.labels = labels_, .selected = selected_, .select = select_});
        }
        return detail::lower(body, context);
    }

private:
    std::vector<std::string> labels_;
    std::function<std::size_t()> selected_;
    std::function<void(std::size_t)> select_;
};

// A segment: the selected one is raised.
class PickerSegmentStyle final : public ButtonStyle {
public:
    explicit PickerSegmentStyle(std::function<bool()> selected) : selected_(std::move(selected)) {}

    View make_body(const ButtonConfiguration& button) const override {
        const auto theme = Environment<ThemeKey>();
        const auto scheme = Environment<ColorSchemeKey>();
        const auto colors = [=] { return theme().colors(scheme()); };
        const auto selected = selected_;
        return button.label.font(Font::system(13, Weight::medium))
            .foreground_style([=] {
                const ColorTokens c = colors();
                if (!button.is_enabled()) {
                    return c.tertiary;
                }
                return selected() || button.is_hovered() ? c.primary : c.secondary;
            })
            .padding(EdgeInsets::symmetric(12, 5))
            .frame({.max_width = infinity})
            .background(
                [=] {
                    const ColorTokens c = colors();
                    if (selected()) {
                        return c.surface_raised;
                    }
                    return button.is_pressed() ? c.outline.opacity(0.5f) : Color::clear;
                },
                RoundedRectangle(6));
    }

private:
    std::function<bool()> selected_;
};

} // namespace

} // namespace elg::detail

namespace elg {

using detail::ViewAccess;

View Slider(Binding<float> value, SliderOptions options) {
    return ViewAccess::make(std::make_shared<const detail::SliderNode>(std::move(value), std::move(options)));
}

View KnobSliderStyle::make_track(const SliderConfiguration&) const {
    return Capsule()
        .fill([track = options_.track](const EnvironmentValues& environment) { return track.get(environment); })
        .frame({.height = options_.track_thickness});
}

View KnobSliderStyle::make_fill(const SliderConfiguration& slider) const {
    const auto tint = Environment<TintKey>();
    const std::optional<Prop<Color>> fill = options_.fill;
    return Capsule()
        .fill([=](const EnvironmentValues& environment) {
            const Color base = fill ? fill->get(environment) : tint();
            return slider.is_enabled() ? base : base.opacity(0.4f);
        })
        .frame({.height = options_.track_thickness});
}

View KnobSliderStyle::make_thumb(const SliderConfiguration& slider) const {
    const auto tint = Environment<TintKey>();
    const auto theme = Environment<ThemeKey>();
    const auto scheme = Environment<ColorSchemeKey>();
    const auto colors = [=] { return theme().colors(scheme()); };
    const float size = options_.knob_size;
    const View knob = Circle()
                          .fill([=] {
                              if (!slider.is_enabled()) {
                                  return colors().surface_raised;
                              }
                              if (slider.is_pressed()) {
                                  return Color::white.mix(Color::black, 0.08f);
                              }
                              return slider.is_hovered() ? Color::white.mix(tint(), 0.08f) : Color::white;
                          })
                          .border([=] { return colors().outline.opacity(0.6f); }, 0.5f, Circle())
                          .frame(size, size);
    return If(slider.is_enabled, knob.shadow({.color = Color::black.opacity(0.3f), .radius = 4, .y = 1}), knob);
}

View SegmentedPickerStyle::make_body(const PickerConfiguration& picker) const {
    std::vector<View> segments;
    segments.reserve(picker.labels.size());
    for (std::size_t i = 0; i < picker.labels.size(); ++i) {
        // Labels keep their width: the stack offers equal shares, and a longer label ("Center"
        // among "End" and "Fill") would wrap even when all of them fit.
        segments.push_back(Button(Text(picker.labels[i]).fixed_size(true, false),
                                  [select = picker.select, i] {
                                      with_animation(Animation::ease_out(0.15), [&] { select(i); });
                                  })
                               .button_style(detail::PickerSegmentStyle(
                                   [selected = picker.selected, i] { return selected() == i; })));
    }
    return HStackLayout(VerticalAlignment::center, 2)(std::move(segments))
        .padding(2)
        .background(ThemeColor::outline, RoundedRectangle(8))
        .fixed_size(false, true);
}

View detail::make_picker(std::vector<std::string> labels, std::function<std::size_t()> selected,
                         std::function<void(std::size_t)> select) {
    return ViewAccess::make(
        std::make_shared<const PickerNode>(std::move(labels), std::move(selected), std::move(select)));
}

} // namespace elg
