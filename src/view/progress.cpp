// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Progress: a fraction or activity, drawn by a ProgressStyle; the built-in bar and ring draw
// themselves.

#include "view/animated.h"
#include "view/view_node.h"

#include <elg/graphics_context.h>
#include <elg/path.h>
#include <elg/styles.h>
#include <elg/views.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <numbers>
#include <optional>
#include <utility>

namespace elg::detail {

namespace {

constexpr float kDefaultWidth = 160;

// The control around the style's body: a progress indicator to assistive technology.
class ProgressElement final : public Element {
public:
    ProgressElement(std::unique_ptr<Element> content, std::function<std::optional<float>()> fraction)
        : content_(std::move(content)), fraction_(std::move(fraction)) {
        adopt(*content_);
    }

    void inspect(Inspection& out) const override { out.name = "Progress"; }
    float layout_priority() const noexcept override { return content_->layout_priority(); }
    float z_index() const noexcept override { return content_->z_index(); }
    void for_each_child(const std::function<void(Element&)>& visit) override { visit(*content_); }

    void accessibility(AccessibilityInfo& out) const override {
        out.role = AccessRole::progress;
        if (const std::optional<float> fraction = untrack(fraction_)) {
            out.numeric = std::clamp(*fraction, 0.f, 1.f);
            out.minimum = 0;
            out.maximum = 1;
        }
    }

protected:
    Size measure(ProposedSize proposal) override { return content_->size_that_fits(proposal); }
    void arrange(Rect bounds, ProposedSize proposal) override { content_->place({bounds.x, bounds.y}, proposal); }
    void paint_impl(DisplayList& list) const override { content_->paint(list); }
    bool hit_impl(Point point, std::vector<Element*>& path) override { return content_->hit_test(point, path); }

private:
    std::unique_ptr<Element> content_;
    std::function<std::optional<float>()> fraction_;
};

class ProgressNode final : public ViewNode {
public:
    explicit ProgressNode(Prop<std::optional<float>> fraction) : fraction_(std::move(fraction)) {}

    std::unique_ptr<Element> lower(const LowerContext& context) const override {
        const EnvironmentValues env = context.environment;
        const auto fraction = [prop = fraction_, env] { return prop.get(env); };
        const std::shared_ptr<const ProgressStyle> style = untrack([&] { return env.get<ProgressStyleKey>(); });
        View body;
        {
            const EnvironmentScope scope(env);
            body = style->make_body({.fraction = fraction});
        }
        return std::make_unique<ProgressElement>(detail::lower(body, context.nested()), fraction);
    }

private:
    Prop<std::optional<float>> fraction_;
};

// --- The built-in styles' bar and ring ---

struct IndicatorSpec {
    bool circular = false;
    float thickness = 6; // the bar's height, or the ring's line width
    float size = 20;     // the ring's diameter
};

class IndicatorElement final : public Element {
public:
    explicit IndicatorElement(IndicatorSpec spec) : spec_(spec) {}

    void set_fraction(std::optional<float> fraction, const Animation* animation) {
        // Becoming determinate starts from the value instead of sliding from the last one.
        const bool was = determinate_;
        determinate_ = fraction.has_value();
        if (fraction) {
            fraction_.set(std::clamp(*fraction, 0.f, 1.f), was ? animation : nullptr);
        }
    }
    void set_track(Color color) {
        track_ = color;
    }
    void set_fill(Color color) {
        fill_ = color;
    }

protected:
    Size measure(ProposedSize proposal) override {
        if (spec_.circular) {
            return {spec_.size, spec_.size};
        }
        const float width = proposal.width.value_or(kDefaultWidth);
        return {std::isfinite(width) ? width : kDefaultWidth, spec_.thickness};
    }
    void arrange(Rect, ProposedSize) override {}
    void paint_impl(DisplayList& list) const override {
        if (determinate_ ? fraction_.animating() : true) {
            list.animating = true;
        }
        spec_.circular ? paint_ring(list) : paint_bar(list);
    }

private:
    static RectCommand capsule(Rect rect, Color color) {
        return RectCommand{rect, CornerRadii::all(std::min(rect.width, rect.height) / 2), ShapeStyle(color),
                           Color::clear, 0};
    }

    void paint_bar(DisplayList& list) const {
        const Rect bounds = frame();
        list.commands.push_back(capsule(bounds, track_));
        if (determinate_) {
            const float width = bounds.width * fraction_.current();
            if (width > 0) {
                list.commands.push_back(capsule({bounds.x, bounds.y, std::max(width, bounds.height), bounds.height}, fill_));
            }
            return;
        }
        // A segment sweeping across, easing in and out each pass.
        constexpr double kPeriod = 1.4;
        const double t = std::fmod(frame_time(), kPeriod) / kPeriod;
        const float eased = static_cast<float>(t * t * (3 - 2 * t));
        const float segment = bounds.width * 0.3f;
        const float x = bounds.x - segment + (bounds.width + segment) * eased;
        list.commands.push_back(PushClipCommand{bounds, CornerRadii::all(bounds.height / 2)});
        list.commands.push_back(capsule({x, bounds.y, segment, bounds.height}, fill_));
        list.commands.push_back(PopClipCommand{});
    }

    void paint_ring(DisplayList& list) const {
        const Rect bounds = frame();
        const float line = std::min(spec_.thickness, bounds.width / 2);
        const float radius = (std::min(bounds.width, bounds.height) - line) / 2;
        const Point center{bounds.width / 2, bounds.height / 2};
        GraphicsContext g(list, bounds, list.scale);
        const StrokeStyle stroke{.width = line, .cap = LineCap::round};
        g.stroke_path(Path().add_circle(center, radius), track_, {.width = line});

        // Angles in turns, clockwise from the top.
        float start = 0;
        float sweep = 0;
        if (determinate_) {
            sweep = fraction_.current();
        } else {
            // A quarter arc going round once a second.
            start = static_cast<float>(std::fmod(frame_time(), 1.0));
            sweep = 0.25f;
        }
        if (sweep <= 0) {
            return;
        }
        const int segments = std::max(2, static_cast<int>(std::ceil(sweep * 64)));
        Path arc;
        for (int i = 0; i <= segments; ++i) {
            const float turn = start + sweep * static_cast<float>(i) / static_cast<float>(segments);
            const float angle = (turn - 0.25f) * 2 * std::numbers::pi_v<float>;
            const Point p{center.x + radius * std::cos(angle), center.y + radius * std::sin(angle)};
            i == 0 ? arc.move_to(p) : arc.line_to(p);
        }
        g.stroke_path(arc, fill_, stroke);
    }

    IndicatorSpec spec_;
    bool determinate_ = false;
    Animated<float> fraction_{0.f};
    Color track_;
    Color fill_;
};

class IndicatorNode final : public ViewNode {
public:
    IndicatorNode(Signal<std::optional<float>> fraction, IndicatorSpec spec, Prop<Color> track,
                  std::optional<Prop<Color>> fill)
        : fraction_(std::move(fraction)), spec_(spec), track_(std::move(track)), fill_(std::move(fill)) {}

    std::unique_ptr<Element> lower(const LowerContext& context) const override {
        auto element = std::make_unique<IndicatorElement>(spec_);
        IndicatorElement* const target = element.get();
        detail::bind(Prop<std::optional<float>>(fraction_), context,
                     [target](std::optional<float> fraction, const Animation* animation) {
                         target->set_fraction(fraction, animation);
                     });
        detail::bind(track_, context, [target](Color color) { target->set_track(color); });
        const Prop<Color> fill =
            fill_.value_or(Prop<Color>([](const EnvironmentValues& env) { return env.get<TintKey>(); }));
        detail::bind(fill, context, [target](Color color) { target->set_fill(color); });
        return element;
    }

private:
    Signal<std::optional<float>> fraction_;
    IndicatorSpec spec_;
    Prop<Color> track_;
    std::optional<Prop<Color>> fill_;
};

} // namespace

} // namespace elg::detail

namespace elg {

using detail::ViewAccess;

View Progress(Prop<std::optional<float>> fraction) {
    return ViewAccess::make(std::make_shared<const detail::ProgressNode>(std::move(fraction)));
}

View LinearProgressStyle::make_body(const ProgressConfiguration& progress) const {
    return ViewAccess::make(std::make_shared<const detail::IndicatorNode>(
        progress.fraction, detail::IndicatorSpec{.thickness = options_.thickness}, options_.track, options_.fill));
}

View CircularProgressStyle::make_body(const ProgressConfiguration& progress) const {
    return ViewAccess::make(std::make_shared<const detail::IndicatorNode>(
        progress.fraction,
        detail::IndicatorSpec{.circular = true, .thickness = options_.line_width, .size = options_.size},
        options_.track, options_.fill));
}

} // namespace elg
