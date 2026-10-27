// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// NumberInput: a TextField editing the number's text, steps (the style's buttons, Up/Down), and
// scrubbing, drawn by a NumberInputStyle; one spin button to assistive technology.

#include "view/view_node.h"

#include <elg/modifier.h>
#include <elg/shapes.h>
#include <elg/styles.h>
#include <elg/views.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace elg::detail {

namespace {

constexpr float kScrubSlop = 3;        // points the pointer moves before a press scrubs
constexpr float kPointsPerStep = 4;    // scrubbed distance per step

std::string default_format(double value, int decimals) {
    if (decimals >= 0) {
        std::string text = std::format("{:.{}f}", value, decimals);
        return text.find_first_not_of("-0.") == std::string::npos ? std::format("{:.{}f}", 0.0, decimals) : text;
    }
    // As many digits as it needs, up to 6.
    std::string text = std::format("{:.6f}", value);
    text.erase(text.find_last_not_of('0') + 1);
    if (text.back() == '.') {
        text.pop_back();
    }
    return text == "-0" ? "0" : text;
}

std::optional<double> default_parse(std::string_view text) {
    const std::size_t begin = text.find_first_not_of(" \t");
    if (begin == std::string_view::npos) {
        return std::nullopt;
    }
    const std::string trimmed(text.substr(begin, text.find_last_not_of(" \t") - begin + 1));
    char* end = nullptr;
    const double value = std::strtod(trimmed.c_str(), &end);
    if (end != trimmed.c_str() + trimmed.size() || !std::isfinite(value)) {
        return std::nullopt;
    }
    return value;
}

// Scrubbing: a press that moves sideways changes the value; one that doesn't is a click.
struct ScrubActions {
    std::function<void()> begin;
    std::function<void(float)> drag; // the distance from the press
    std::function<void()> click;
};

class ScrubHandler final : public EventHandler {
public:
    explicit ScrubHandler(ScrubActions actions) : actions_(std::move(actions)) {}

    EventResult pointer(const PointerEvent& event, Rect) override {
        if (event.button != MouseButton::left && event.kind != PointerEvent::Kind::cancel) {
            return {};
        }
        switch (event.kind) {
        case PointerEvent::Kind::hover: return {};
        case PointerEvent::Kind::down:
            pressed_ = true;
            scrubbing_ = false;
            start_ = event.location.x;
            return {.consumed = true};
        case PointerEvent::Kind::move: {
            if (!pressed_) {
                return {};
            }
            const float dx = event.location.x - start_;
            if (!scrubbing_ && std::abs(dx) < kScrubSlop) {
                return {.consumed = true};
            }
            if (!std::exchange(scrubbing_, true)) {
                actions_.begin();
            }
            actions_.drag(dx);
            return {.consumed = true, .redraw = true};
        }
        case PointerEvent::Kind::up:
            if (std::exchange(pressed_, false) && !scrubbing_) {
                actions_.click();
            }
            scrubbing_ = false;
            return {.consumed = true, .redraw = true};
        case PointerEvent::Kind::cancel:
            pressed_ = scrubbing_ = false;
            return {};
        }
        return {};
    }
    PointerStyle pointer_style(Point) const override { return PointerStyle::resize_horizontal; }
    bool covers_frame() const override { return true; }

private:
    ScrubActions actions_;
    bool pressed_ = false;
    bool scrubbing_ = false;
    float start_ = 0;
};

class ScrubModifier final : public EventModifier {
public:
    explicit ScrubModifier(ScrubActions actions) : actions_(std::move(actions)) {}
    std::unique_ptr<EventHandler> make_handler() const override { return std::make_unique<ScrubHandler>(actions_); }

private:
    ScrubActions actions_;
};

// The control around the style's body: a spin button to assistive technology.
class NumberInputElement final : public Element {
public:
    struct Access {
        std::function<double()> value;
        std::function<double()> min;
        std::function<double()> max;
        std::function<double()> step;
        std::function<void(double)> set;
        std::function<void()> increment;
        std::function<void()> decrement;
        std::function<bool()> enabled;
    };

    NumberInputElement(std::unique_ptr<Element> content, Access access)
        : content_(std::move(content)), access_(std::move(access)) {
        adopt(*content_);
    }

    void inspect(Inspection& out) const override { out.name = "NumberInput"; }
    float layout_priority() const noexcept override { return content_->layout_priority(); }
    float z_index() const noexcept override { return content_->z_index(); }
    void for_each_child(const std::function<void(Element&)>& visit) override { visit(*content_); }

    void accessibility(AccessibilityInfo& out) const override {
        untrack([&] {
            out.role = AccessRole::stepper;
            out.numeric = access_.value();
            if (const double min = access_.min(); std::isfinite(min)) {
                out.minimum = min;
            }
            if (const double max = access_.max(); std::isfinite(max)) {
                out.maximum = max;
            }
            out.step = access_.step();
            out.disabled = !access_.enabled();
        });
        out.can_adjust = out.can_set_value = !out.disabled;
    }
    bool accessibility_action(const AccessibilityAction& action) override {
        if (!untrack(access_.enabled)) {
            return false;
        }
        switch (action.kind) {
        case AccessibilityAction::Kind::increment: access_.increment(); return true;
        case AccessibilityAction::Kind::decrement: access_.decrement(); return true;
        case AccessibilityAction::Kind::set_numeric: access_.set(action.number); return true;
        default: return false;
        }
    }

protected:
    Size measure(ProposedSize proposal) override { return content_->size_that_fits(proposal); }
    void arrange(Rect bounds, ProposedSize proposal) override { content_->place({bounds.x, bounds.y}, proposal); }
    void paint_impl(DisplayList& list) const override { content_->paint(list); }
    bool hit_impl(Point point, std::vector<Element*>& path) override { return content_->hit_test(point, path); }

private:
    std::unique_ptr<Element> content_;
    Access access_;
};

class NumberInputNode final : public ViewNode {
public:
    NumberInputNode(Binding<double> value, bool integer, NumberInputOptions options)
        : value_(std::move(value)), integer_(integer), options_(std::move(options)) {}

    std::unique_ptr<Element> lower(const LowerContext& context) const override;

private:
    Binding<double> value_;
    bool integer_;
    NumberInputOptions options_;
};

std::unique_ptr<Element> NumberInputNode::lower(const LowerContext& context) const {
    const EnvironmentValues env = context.environment;
    const NumberInputOptions o = options_;
    const Binding<double> value = value_;
    const bool integer = integer_;

    const auto min = [o, env] { return o.min.get(env); };
    const auto max = [o, env] { return std::max(o.max.get(env), o.min.get(env)); };
    const auto step = [o, env] { return o.step.get(env); };
    const auto clamp = [=](double v) {
        v = std::clamp(v, min(), max());
        return integer ? std::round(v) : v;
    };
    const auto format = [=](double v) {
        return o.format ? o.format(v) : default_format(v, integer ? 0 : o.decimals.get(env));
    };
    const auto parse = [o](std::string_view text) { return o.parse ? o.parse(text) : default_parse(text); };

    // The text being edited; while the field isn't, it follows the value.
    const State<std::string> text;
    const State<bool> focused(false);
    const State<bool> hovered(false);
    const auto dirty = std::make_shared<bool>(false); // typed since the value was last shown
    effect([=] {
        std::string shown = format(value());
        if (!focused.peek()) {
            text = std::move(shown);
        }
    });

    // Typing applies on Enter and when the field loses focus; text that isn't a number reverts.
    const auto commit = [=] {
        untrack([&] {
            if (std::exchange(*dirty, false)) {
                if (const std::optional<double> typed = parse(text.peek())) {
                    value.set(clamp(*typed));
                }
            }
            text = format(value());
        });
    };
    effect([=] {
        if (!focused()) {
            commit();
        }
    });
    const auto set = [=](double v) {
        untrack([&] {
            value.set(clamp(v));
            *dirty = false;
            text = format(value());
        });
    };
    // From what's typed, if it's a number, else from the value.
    const auto step_by = [=](double steps) {
        untrack([&] {
            double base = value();
            if (*dirty) {
                base = parse(text.peek()).value_or(base);
            }
            set(base + steps * step());
        });
    };
    const auto increment = [=] { step_by(1); };
    const auto decrement = [=] { step_by(-1); };

    const Binding<std::string> field_text([=] { return text(); },
                                          [=](std::string typed) {
                                              *dirty = true;
                                              text = typed;
                                              if (untrack([&] { return o.live.get(env); })) {
                                                  const std::optional<double> v = parse(typed);
                                                  if (v && *v >= untrack(min) && *v <= untrack(max)) {
                                                      value.set(integer ? std::round(*v) : *v);
                                                  }
                                              }
                                          });
    const auto on_key = [=](const KeyEvent& event) {
        if (event.kind != KeyEvent::Kind::down || (event.key != Key::up && event.key != Key::down)) {
            return false;
        }
        const double steps = has(event.modifiers, KeyModifiers::shift) ? 10 : 1;
        step_by(event.key == Key::up ? steps : -steps);
        return true;
    };

    const auto scrub_start = std::make_shared<double>(0);
    const ScrubActions scrub{
        .begin = [=] { *scrub_start = untrack(value); },
        .drag = [=](float dx) { set(*scrub_start + std::round(static_cast<double>(dx / kPointsPerStep)) * untrack(step)); },
        .click = [=] { focused = true; },
    };
    const View field = TextField("", field_text, {.on_submit = commit, .on_key = on_key})
                           .text_field_style(PlainTextFieldStyle{})
                           .focused(focused.binding())
                           .overlay(If([=] { return o.scrubs.get(env) && !focused(); },
                                       Rectangle().fill(Color::clear).modifier(ScrubModifier(scrub))));

    const NumberInputConfiguration configuration{
        .field = field,
        .increment = increment,
        .decrement = decrement,
        .can_increment = [=] { return value() < max(); },
        .can_decrement = [=] { return value() > min(); },
        .shows_buttons = [=] { return o.shows_buttons.get(env); },
        .is_focused = focused,
        .is_hovered = hovered,
        .is_enabled = [env] { return !env.get<DisabledKey>(); },
    };
    const std::shared_ptr<const NumberInputStyle> style = untrack([&] { return env.get<NumberInputStyleKey>(); });
    View body;
    {
        const EnvironmentScope scope(env);
        body = style->make_body(configuration).on_pointer_enter([hovered] { hovered = true; }).on_pointer_leave([hovered] { hovered = false; });
    }
    return std::make_unique<NumberInputElement>(detail::lower(body, context.nested()),
                                                NumberInputElement::Access{
                                                    .value = [value] { return value(); },
                                                    .min = min,
                                                    .max = max,
                                                    .step = step,
                                                    .set = set,
                                                    .increment = increment,
                                                    .decrement = decrement,
                                                    .enabled = [env] { return !env.get<DisabledKey>(); },
                                                });
}

} // namespace

View make_number_input(Binding<double> value, bool integer, NumberInputOptions options) {
    return ViewAccess::make(std::make_shared<const NumberInputNode>(std::move(value), integer, std::move(options)));
}

} // namespace elg::detail
