// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// The environment's theme resolution and the built-in button styles. The styles use only the
// public API, like an app's own styles would (design.md §9.4, dogfooding).

#include "view/icons.h"

#include <elg/animation.h>
#include <elg/environment.h>
#include <elg/error.h>
#include <elg/shapes.h>
#include <elg/styles.h>
#include <elg/text.h>
#include <elg/views.h>

namespace elg {

namespace detail {

Color resolve_theme_color(const EnvironmentValues& environment, ThemeColor token) {
    return environment.get<ThemeKey>().colors(environment.get<ColorSchemeKey>())[token];
}

float resolve_theme_blur(const EnvironmentValues& environment, ThemeBlur token) {
    return environment.get<ThemeKey>().blur[token];
}

void throw_needs_environment() {
    throw UsageError("this value depends on the environment (e.g. a ThemeColor); it can only be read "
                     "where it's used in a view");
}

void throw_no_environment() {
    throw UsageError("Environment<Key>() can only be used while views are built: in a body, a "
                     "ViewModifier's body, or a style's make_body");
}

} // namespace detail

std::shared_ptr<const ButtonStyle> ButtonStyleKey::default_value(const EnvironmentValues&) {
    static const auto style = std::make_shared<const ProminentButtonStyle>();
    return style;
}

ButtonStyle::~ButtonStyle() = default;

std::shared_ptr<const ToggleStyle> ToggleStyleKey::default_value(const EnvironmentValues&) {
    static const auto style = std::make_shared<const SwitchToggleStyle>();
    return style;
}

ToggleStyle::~ToggleStyle() = default;

std::shared_ptr<const CheckboxStyle> CheckboxStyleKey::default_value(const EnvironmentValues&) {
    static const auto style = std::make_shared<const BoxCheckboxStyle>();
    return style;
}

CheckboxStyle::~CheckboxStyle() = default;

std::shared_ptr<const NumberInputStyle> NumberInputStyleKey::default_value(const EnvironmentValues&) {
    static const auto style = std::make_shared<const BoxNumberInputStyle>();
    return style;
}

NumberInputStyle::~NumberInputStyle() = default;

std::shared_ptr<const ProgressStyle> ProgressStyleKey::default_value(const EnvironmentValues&) {
    static const auto style = std::make_shared<const LinearProgressStyle>();
    return style;
}

ProgressStyle::~ProgressStyle() = default;

std::shared_ptr<const PickerStyle> PickerStyleKey::default_value(const EnvironmentValues&) {
    static const auto style = std::make_shared<const SegmentedPickerStyle>();
    return style;
}

PickerStyle::~PickerStyle() = default;

std::shared_ptr<const SliderStyle> SliderStyleKey::default_value(const EnvironmentValues&) {
    static const auto style = std::make_shared<const KnobSliderStyle>();
    return style;
}

SliderStyle::~SliderStyle() = default;

View SliderStyle::make_fill(const SliderConfiguration&) const {
    return View();
}

std::shared_ptr<const PopoverStyle> PopoverStyleKey::default_value(const EnvironmentValues&) {
    static const auto style = std::make_shared<const DefaultPopoverStyle>();
    return style;
}

PopoverStyle::~PopoverStyle() = default;

std::shared_ptr<const TooltipStyle> TooltipStyleKey::default_value(const EnvironmentValues&) {
    static const auto style = std::make_shared<const DefaultTooltipStyle>();
    return style;
}

TooltipStyle::~TooltipStyle() = default;

std::shared_ptr<const MenuStyle> MenuStyleKey::default_value(const EnvironmentValues&) {
    static const auto style = std::make_shared<const DefaultMenuStyle>();
    return style;
}

MenuStyle::~MenuStyle() = default;

std::shared_ptr<const RowStyle> RowStyleKey::default_value(const EnvironmentValues&) {
    static const auto style = std::make_shared<const HighlightRowStyle>();
    return style;
}

RowStyle::~RowStyle() = default;

View HighlightRowStyle::make_body(const RowConfiguration& row) const {
    const HighlightRowStyleOptions options = options_;
    const FlexibleFrameOptions span = row.axis == Axis::vertical
                                          ? FlexibleFrameOptions{.max_width = infinity, .alignment = Alignment::leading}
                                          : FlexibleFrameOptions{.max_height = infinity, .alignment = Alignment::top};
    const auto highlight = [=](const EnvironmentValues& env) {
        if (row.is_selected()) {
            const Color color = options.selected ? options.selected->get(env) : env.get<TintKey>();
            return row.is_enabled() ? color : color.opacity(0.5f);
        }
        return row.is_hovered() && row.is_enabled() ? options.hovered.get(env).opacity(0.5f) : Color::clear;
    };
    const auto ring = [=](const EnvironmentValues& env) {
        return row.is_focused() && !row.is_selected() ? env.get<TintKey>() : Color::clear;
    };
    // Unselected rows keep the foreground around them.
    return row.content.padding(options.padding)
        .frame(span)
        .foreground_style([=](const EnvironmentValues& env) {
            return row.is_selected() ? options.on_selected.get(env) : env.get<ForegroundKey>();
        })
        .background(highlight, RoundedRectangle(options.corner_radius))
        .overlay(RoundedRectangle(options.corner_radius).stroke_border(ring, 2));
}

View PlainRowStyle::make_body(const RowConfiguration& row) const {
    return row.content;
}

std::shared_ptr<const TextFieldStyle> TextFieldStyleKey::default_value(const EnvironmentValues&) {
    static const auto style = std::make_shared<const RoundedBorderTextFieldStyle>();
    return style;
}

TextFieldStyle::~TextFieldStyle() = default;

View RoundedBorderTextFieldStyle::make_body(const TextFieldConfiguration& field) const {
    constexpr float kRadius = 7;
    const auto tint = Environment<TintKey>();
    const auto theme = Environment<ThemeKey>();
    const auto scheme = Environment<ColorSchemeKey>();
    const auto colors = [=] { return theme().colors(scheme()); };
    return field.field.padding(EdgeInsets::symmetric(10, 7))
        .background(
            [=] {
                const Color base = colors().surface_raised;
                return field.is_enabled() ? base : base.opacity(0.5f);
            },
            RoundedRectangle(kRadius))
        .overlay(RoundedRectangle(kRadius).stroke_border(
            [=] {
                const ColorTokens c = colors();
                return field.is_hovered() && field.is_enabled() ? c.outline.mix(c.primary, 0.25f) : c.outline;
            },
            1))
        // Focus: a 2 pt ring in the tint just outside the outline.
        .overlay(RoundedRectangle(kRadius + 2)
                     .stroke_border(tint, 2)
                     .padding(-2)
                     .opacity([=] { return field.is_focused() ? 1.f : 0.f; })
                     .animation(Animation::ease_out(0.12), field.is_focused));
}

View PlainTextFieldStyle::make_body(const TextFieldConfiguration& field) const {
    return field.field;
}

namespace {

constexpr EdgeInsets kButtonPadding = EdgeInsets::symmetric(14, 7);
constexpr float kButtonRadius = 8;

// A label font: the environment's, semibold.
Prop<Font> label_font() {
    const auto font = Environment<FontKey>();
    return [font] { return font().weight(Weight::semibold); };
}

} // namespace

View ProminentButtonStyle::make_body(const ButtonConfiguration& button) const {
    const auto tint = Environment<TintKey>();
    return button.label.font(label_font())
        .foreground_style(ThemeColor::on_accent)
        .padding(kButtonPadding)
        .background(
            [=] {
                const Color base = tint();
                if (!button.is_enabled()) {
                    return base.opacity(0.4f);
                }
                if (button.is_pressed()) {
                    return base.mix(Color::black, 0.2f);
                }
                return button.is_hovered() ? base.mix(Color::white, 0.15f) : base;
            },
            RoundedRectangle(kButtonRadius));
}

View BorderedButtonStyle::make_body(const ButtonConfiguration& button) const {
    const auto tint = Environment<TintKey>();
    const auto surface = Environment<ThemeKey>();
    const auto scheme = Environment<ColorSchemeKey>();
    const auto colors = [=] { return surface().colors(scheme()); };
    return button.label.font(label_font())
        .foreground_style([=] { return button.is_enabled() ? tint() : colors().tertiary; })
        .padding(kButtonPadding)
        .background(
            [=] {
                const Color base = colors().surface_raised;
                if (button.is_pressed()) {
                    return base.mix(colors().outline, 0.6f);
                }
                return button.is_hovered() ? base.mix(colors().outline, 0.35f) : base;
            },
            RoundedRectangle(kButtonRadius))
        .overlay(RoundedRectangle(kButtonRadius).stroke_border(ThemeColor::outline, 1));
}

View PlainButtonStyle::make_body(const ButtonConfiguration& button) const {
    // As SwiftUI's .plain: no tint, the label keeps the colors it would have anyway.
    return button.label.opacity([=] {
        if (!button.is_enabled()) {
            return 0.4f;
        }
        return button.is_pressed() ? 0.6f : 1.f;
    });
}

View SwitchToggleStyle::make_body(const ToggleConfiguration& toggle) const {
    const SwitchToggleStyleOptions& o = options_;
    const float knob = std::max(o.height - 2 * o.knob_inset, 0.f);
    const float travel = (o.width - o.height) / 2;
    const std::optional<Prop<Color>> on = o.on;
    const Prop<Color> off = o.off;

    View track = Capsule()
                     .fill([=](const EnvironmentValues& env) {
                         Color base = !toggle.is_on() ? off.get(env)
                                      : on            ? on->get(env)
                                                      : env.get<TintKey>();
                         if (!toggle.is_enabled()) {
                             return base.opacity(0.4f);
                         }
                         if (toggle.is_pressed()) {
                             return base.mix(Color::black, 0.15f);
                         }
                         return toggle.is_hovered() ? base.mix(Color::white, 0.1f) : base;
                     })
                     .frame(o.width, o.height)
                     .overlay(Circle()
                                  .fill([knob_color = o.knob](const EnvironmentValues& env) { return knob_color.get(env); })
                                  .frame(knob, knob)
                                  .shadow({.color = Color::black.opacity(0.25f), .radius = 3, .y = 1})
                                  .offset([=] { return toggle.is_on() ? travel : -travel; }, 0.f));
    if (toggle.label.is_empty()) { // Toggle(binding): no label, and no space for one
        return o.fills_width ? HStack({.spacing = 0}, Spacer(), std::move(track)) : track;
    }
    View label = toggle.label.foreground_style([=](const EnvironmentValues& env) {
        return env.color(toggle.is_enabled() ? ThemeColor::primary : ThemeColor::tertiary);
    });
    if (o.fills_width) {
        return HStack({.spacing = o.spacing}, std::move(label), Spacer(), std::move(track));
    }
    return HStack({.spacing = o.spacing}, std::move(label), std::move(track));
}

View BoxCheckboxStyle::make_body(const CheckboxConfiguration& checkbox) const {
    const BoxCheckboxStyleOptions& o = options_;
    const std::optional<Prop<Color>> fill = o.fill;
    const Prop<Color> empty = o.empty;
    const Prop<Color> border = o.border;
    const auto marked = [=] { return checkbox.is_checked() || checkbox.is_mixed(); };
    const auto dim = [=](Color color) { return checkbox.is_enabled() ? color : color.opacity(0.4f); };
    const auto filled = [=](const EnvironmentValues& env) { return fill ? fill->get(env) : env.get<TintKey>(); };

    View check = o.check.value_or(detail::icon(detail::Icon::check, o.size * 0.72f, Weight::bold));
    View dash = o.dash.value_or(
        Capsule().fill([mark = o.mark](const EnvironmentValues& env) { return mark.get(env); }).frame(o.size * 0.5f, 2));
    View box = RoundedRectangle(o.corner_radius)
                   .fill([=](const EnvironmentValues& env) {
                       const Color base = marked() ? filled(env) : empty.get(env);
                       return dim(checkbox.is_pressed() ? base.mix(Color::black, 0.15f) : base);
                   })
                   .frame(o.size, o.size)
                   .overlay(RoundedRectangle(o.corner_radius)
                                .stroke_border([=](const EnvironmentValues& env) {
                                    return dim(marked() ? filled(env) : border.get(env));
                                }, o.border_width))
                   .overlay(std::move(check).foreground_style(o.mark).opacity([=] {
                       return checkbox.is_checked() && !checkbox.is_mixed() ? 1.f : 0.f;
                   }))
                   .overlay(std::move(dash).opacity([=] { return checkbox.is_mixed() ? 1.f : 0.f; }));
    if (checkbox.label.is_empty()) { // Checkbox(binding): no label, and no space for one
        return box;
    }
    View label = checkbox.label.foreground_style([=](const EnvironmentValues& env) {
        return env.color(checkbox.is_enabled() ? ThemeColor::primary : ThemeColor::tertiary);
    });
    return HStack({.spacing = o.spacing}, std::move(box), std::move(label));
}

namespace {

// A number input's stacked arrow: highlighted while hovered or pressed.
class ArrowButtonStyle final : public ButtonStyle {
public:
    View make_body(const ButtonConfiguration& button) const override {
        return button.label
            .foreground_style([=](const EnvironmentValues& env) {
                return env.color(button.is_enabled() ? ThemeColor::secondary : ThemeColor::tertiary);
            })
            .padding(EdgeInsets::symmetric(3, 0))
            .background(
                [=](const EnvironmentValues& env) {
                    const Color outline = env.color(ThemeColor::outline);
                    if (!button.is_enabled()) {
                        return Color::clear;
                    }
                    return button.is_pressed() ? outline : button.is_hovered() ? outline.opacity(0.5f) : Color::clear;
                },
                RoundedRectangle(3));
    }
};

// The field, then (if shown) the arrows stacked at its trailing edge.
View number_row(const NumberInputConfiguration& input, float arrow_size) {
    const auto arrow = [&](detail::Icon which, std::function<void()> action, Signal<bool> can, const char* name) {
        return Button(detail::icon(which, arrow_size, Weight::bold), std::move(action))
            .button_style(ArrowButtonStyle{})
            .disabled([can] { return !can(); })
            .accessibility_label(name);
    };
    const View arrows = VStack({.spacing = 0},
                               arrow(detail::Icon::chevron_up, input.increment, input.can_increment, "Increase"),
                               arrow(detail::Icon::chevron_down, input.decrement, input.can_decrement, "Decrease"));
    return HStack({.spacing = 4}, input.field, If(input.shows_buttons, arrows));
}

} // namespace

View BoxNumberInputStyle::make_body(const NumberInputConfiguration& input) const {
    const BoxNumberInputStyleOptions& o = options_;
    const Prop<Color> background = o.background;
    const Prop<Color> border = o.border;
    const std::optional<Prop<Color>> focused_border = o.focused_border;
    return number_row(input, o.arrow_size)
        .padding(o.padding)
        .background([=](const EnvironmentValues& env) { return background.get(env); }, RoundedRectangle(o.corner_radius))
        .overlay(RoundedRectangle(o.corner_radius)
                     .stroke_border(
                         [=](const EnvironmentValues& env) {
                             if (!input.is_focused()) {
                                 return border.get(env);
                             }
                             return focused_border ? focused_border->get(env) : env.get<TintKey>();
                         },
                         o.border_width))
        .opacity([=] { return input.is_enabled() ? 1.f : 0.5f; });
}

View PlainNumberInputStyle::make_body(const NumberInputConfiguration& input) const {
    return number_row(input, 9);
}

} // namespace elg
