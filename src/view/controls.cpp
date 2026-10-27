// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Slider and ProgressView (elements that draw themselves), and Stepper and the segmented Picker
// (composed from the public API, like an app's own controls would be).

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

RectCommand capsule(Rect rect, Color color) {
    return RectCommand{rect, CornerRadii::all(std::min(rect.width, rect.height) / 2), ShapeStyle(color), Color::clear, 0};
}

constexpr float kDefaultWidth = 160;

// --- Slider ---

class SliderElement final : public Element, private EventHandler {
public:
    SliderElement(Binding<float> value, SliderOptions options, EnvironmentValues environment)
        : value_(std::move(value)), options_(std::move(options)), environment_(std::move(environment)) {}

    void set_value(float value, const Animation* animation) { shown_.set(fraction(value), animation); }
    // SliderOptions' min, max, and step; the knob moves to where the value is in the new range.
    void set_range(float min, float max, float step) {
        min_ = min;
        max_ = max > min ? max : min + 1;
        step_ = step;
        shown_.set(fraction(untrack([&] { return value_(); })), nullptr);
    }

    EventHandler* event_handler() noexcept override { return this; }

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
        const float width = proposal.width.value_or(kDefaultWidth);
        return {std::isfinite(width) ? width : kDefaultWidth, kKnob + 2};
    }
    void arrange(Rect, ProposedSize) override {}
    void paint_impl(DisplayList& list) const override;
    bool hit_impl(Point point, std::vector<Element*>&) override { return contains(frame(), point); }

private:
    static constexpr float kKnob = 20;
    static constexpr float kTrack = 4;

    float fraction(float value) const {
        return std::clamp((value - min_) / (max_ - min_), 0.f, 1.f);
    }
    // What a screen reader's increment moves: the step, else a twentieth of the range.
    float adjust_step() const { return step_ > 0 ? step_ : (max_ - min_) / 20; }
    bool enabled() const {
        return !untrack([&] { return environment_.get<DisabledKey>(); });
    }
    float track_start() const { return frame().x + kKnob / 2; }
    float track_length() const { return std::max(frame().width - kKnob, 1.f); }

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
            if (std::exchange(dragging_, false) && options_.on_editing_changed) {
                options_.on_editing_changed(false);
            }
            return {.consumed = true, .redraw = true};
        }
        return {};
    }

    EventResult hover(bool inside) override {
        hovered_ = inside;
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
    Animated<float> shown_{0.f}; // the knob's position, 0...1
    bool dragging_ = false;
    bool hovered_ = false;
};

void SliderElement::paint_impl(DisplayList& list) const {
    if (shown_.animating()) {
        list.animating = true;
    }
    const Rect bounds = frame();
    const bool active = enabled();
    const Color tint = environment_.get<TintKey>();
    const ColorTokens colors = environment_.get<ThemeKey>().colors(environment_.get<ColorSchemeKey>());
    const float center_y = bounds.y + bounds.height / 2;
    const float knob_x = track_start() + shown_.current() * track_length();

    const Rect track{track_start(), center_y - kTrack / 2, track_length(), kTrack};
    list.commands.push_back(capsule(track, colors.outline));
    list.commands.push_back(capsule({track.x, track.y, knob_x - track.x, kTrack}, active ? tint : tint.opacity(0.4f)));

    const Rect knob{knob_x - kKnob / 2, center_y - kKnob / 2, kKnob, kKnob};
    if (active) {
        list.commands.push_back(BoxShadowCommand{{knob.x, knob.y + 1, knob.width, knob.height}, CornerRadii::all(kKnob / 2),
                                                 Color::black.opacity(0.3f), 4});
    }
    Color face = Color::white;
    if (dragging_) {
        face = face.mix(Color::black, 0.08f);
    } else if (hovered_ && active) {
        face = face.mix(tint, 0.08f);
    }
    list.commands.push_back(RectCommand{knob, CornerRadii::all(kKnob / 2), ShapeStyle(active ? face : colors.surface_raised),
                                        colors.outline.opacity(0.6f), 0.5f});
}

// --- ProgressView ---

class ProgressElement final : public Element {
public:
    ProgressElement(bool determinate, EnvironmentValues environment)
        : determinate_(determinate), environment_(std::move(environment)) {}

    void set_fraction(float fraction, const Animation* animation) {
        fraction_.set(std::clamp(fraction, 0.f, 1.f), animation);
    }

    void accessibility(AccessibilityInfo& out) const override {
        out.role = AccessRole::progress;
        if (determinate_) {
            out.numeric = fraction_.target();
            out.minimum = 0;
            out.maximum = 1;
        }
    }

protected:
    Size measure(ProposedSize proposal) override {
        const float width = proposal.width.value_or(kDefaultWidth);
        return {std::isfinite(width) ? width : kDefaultWidth, kHeight};
    }
    void arrange(Rect, ProposedSize) override {}
    void paint_impl(DisplayList& list) const override {
        const Rect bounds = frame();
        const Color tint = environment_.get<TintKey>();
        const ColorTokens colors = environment_.get<ThemeKey>().colors(environment_.get<ColorSchemeKey>());
        list.commands.push_back(capsule(bounds, colors.outline));
        if (determinate_) {
            if (fraction_.animating()) {
                list.animating = true;
            }
            const float width = bounds.width * fraction_.current();
            if (width > 0) {
                list.commands.push_back(capsule({bounds.x, bounds.y, std::max(width, bounds.height), bounds.height}, tint));
            }
            return;
        }
        // A segment sweeping across, easing in and out each pass.
        list.animating = true;
        constexpr double kPeriod = 1.4;
        const double t = std::fmod(frame_time(), kPeriod) / kPeriod;
        const float eased = static_cast<float>(t * t * (3 - 2 * t));
        const float segment = bounds.width * 0.3f;
        const float x = bounds.x - segment + (bounds.width + segment) * eased;
        list.commands.push_back(PushClipCommand{bounds, CornerRadii::all(bounds.height / 2)});
        list.commands.push_back(capsule({x, bounds.y, segment, bounds.height}, tint));
        list.commands.push_back(PopClipCommand{});
    }

private:
    static constexpr float kHeight = 6;

    bool determinate_;
    EnvironmentValues environment_;
    Animated<float> fraction_{0.f};
};

class SliderNode final : public ViewNode {
public:
    SliderNode(Binding<float> value, SliderOptions options) : value_(std::move(value)), options_(std::move(options)) {}
    std::unique_ptr<Element> lower(const LowerContext& context) const override {
        auto element = std::make_unique<SliderElement>(value_, options_, context.environment);
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
        detail::bind(Prop<float>(value_), context,
                     [target](float value, const Animation* animation) { target->set_value(value, animation); });
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

class ProgressNode final : public ViewNode {
public:
    explicit ProgressNode(std::optional<Prop<float>> fraction) : fraction_(std::move(fraction)) {}
    std::unique_ptr<Element> lower(const LowerContext& context) const override {
        auto element = std::make_unique<ProgressElement>(fraction_.has_value(), context.environment);
        if (fraction_) {
            ProgressElement* const target = element.get();
            detail::bind(*fraction_, context,
                         [target](float value, const Animation* animation) { target->set_fraction(value, animation); });
        }
        return element;
    }

private:
    std::optional<Prop<float>> fraction_;
};

// --- Stepper and Picker: composed from the public API ---

// A picker segment: the selected one is raised.
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

View ProgressView(Prop<float> fraction) {
    return ViewAccess::make(std::make_shared<const detail::ProgressNode>(std::move(fraction)));
}

View ProgressView() {
    return ViewAccess::make(std::make_shared<const detail::ProgressNode>(std::nullopt));
}

View detail::make_segmented_picker(std::vector<std::string> labels, std::function<std::size_t()> selected,
                                   std::function<void(std::size_t)> select) {
    std::vector<View> segments;
    segments.reserve(labels.size());
    for (std::size_t i = 0; i < labels.size(); ++i) {
        // Labels keep their width: the stack offers equal shares, and a longer label ("Center"
        // among "End" and "Fill") would wrap even when all of them fit.
        segments.push_back(Button(Text(labels[i]).fixed_size(true, false),
                                  [select, i] { with_animation(Animation::ease_out(0.15), [&] { select(i); }); })
                               .button_style(PickerSegmentStyle([selected, i] { return selected() == i; })));
    }
    return HStackLayout(VerticalAlignment::center, 2)(std::move(segments))
        .padding(2)
        .background(ThemeColor::outline, RoundedRectangle(8))
        .fixed_size(false, true);
}

} // namespace elg
