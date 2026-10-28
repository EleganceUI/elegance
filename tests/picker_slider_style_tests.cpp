// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// PickerStyle and SliderStyle: a picker is its style's body; a slider places its style's track,
// fill and thumb by the value, and keeps the interaction to itself.

#include "text/text_system.h"
#include "view/accessibility.h"
#include "view/view_node.h"

#include <elg/shapes.h>
#include <elg/styles.h>
#include <elg/views.h>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using namespace elg;

namespace {

detail::TextSystem& text_system() {
    static detail::TextSystem system;
    return system;
}

std::unique_ptr<detail::Element> mount(const View& view, Size size) {
    auto root = detail::lower(view, {.text = &text_system()});
    root->size_that_fits({size.width, size.height});
    root->place({}, {size.width, size.height});
    return root;
}

void relayout(detail::Element& root, Size size) {
    detail::flush_effects();
    root.size_that_fits({size.width, size.height});
    root.place({}, {size.width, size.height});
}

std::vector<detail::Element*> children(detail::Element& element) {
    std::vector<detail::Element*> out;
    element.for_each_child([&](detail::Element& child) { out.push_back(&child); });
    return out;
}

// Shows what it's given, and chooses the last option when activated.
struct LastOptionPickerStyle final : PickerStyle {
    explicit LastOptionPickerStyle(std::vector<std::string>* seen) : labels(seen) {}
    std::vector<std::string>* labels;
    View make_body(const PickerConfiguration& picker) const override {
        *labels = picker.labels;
        return Button(Text([selected = picker.selected] { return std::to_string(selected()); }),
                      [select = picker.select, count = picker.labels.size()] { select(count - 1); });
    }
};

// A 4-point track, a fill, and a 20 x 10 thumb.
struct BlockSliderStyle final : SliderStyle {
    View make_track(const SliderConfiguration&) const override { return Rectangle().frame({.height = 4}); }
    View make_fill(const SliderConfiguration&) const override { return Rectangle(); }
    View make_thumb(const SliderConfiguration&) const override { return Rectangle().frame(20, 10); }
};

} // namespace

TEST_CASE("a picker is its style's body", "[picker][style]") {
    detail::Scope scope;
    detail::Scope::Use use(scope);
    const State<int> size(1);
    std::vector<std::string> labels;
    const auto root = mount(Picker(size.binding(), {{0, "S"}, {1, "M"}, {2, "L"}})
                                .picker_style(LastOptionPickerStyle(&labels)),
                            {200, 40});
    CHECK(labels == std::vector<std::string>{"S", "M", "L"});
    // The button (inside its accessibility wrapper) chooses "L".
    bool chose = false;
    std::function<void(detail::Element&)> press = [&](detail::Element& element) {
        if (!chose && element.accessibility_action({})) {
            chose = true;
            return;
        }
        element.for_each_child(press);
    };
    press(*root);
    detail::flush_effects();
    CHECK(size() == 2);
}

TEST_CASE("the built-in picker styles build", "[picker][style]") {
    detail::Scope scope;
    detail::Scope::Use use(scope);
    const State<std::string> size(std::string("M"));
    const std::vector<std::pair<std::string, std::string>> options{{"S", "Small"}, {"M", "Medium"}};
    CHECK_NOTHROW(mount(Picker(size.binding(), options), {200, 40}));
    CHECK_NOTHROW(mount(Picker(size.binding(), options).picker_style(MenuPickerStyle{}), {200, 40}));
}

TEST_CASE("a slider places its style's parts by the value", "[slider][style]") {
    detail::Scope scope;
    detail::Scope::Use use(scope);
    const State<float> value(5);
    const Size size{220, 30};
    const auto root = mount(Slider(value.binding(), {.min = 0, .max = 10}).slider_style(BlockSliderStyle{}), size);
    const auto parts = children(*root);
    REQUIRE(parts.size() == 3);
    detail::Element& track = *parts[0];
    detail::Element& fill = *parts[1];
    detail::Element& thumb = *parts[2];

    // As tall as the thumb; the track runs between the thumb's centers at either end.
    CHECK(root->frame().height == 10);
    CHECK(track.frame().x == 10);
    CHECK(track.frame().width == 200);
    CHECK(track.frame().height == 4);
    // Halfway: the thumb is centered at 10 + 100, and the fill reaches it.
    CHECK(thumb.frame().x + 10 == 110);
    CHECK(fill.frame().x == 10);
    CHECK(fill.frame().width == 100);
    CHECK(fill.frame().height == 4);

    value = 10;
    relayout(*root, size);
    CHECK(thumb.frame().x + 10 == 210);
    CHECK(fill.frame().width == 200);

    // Keys and accessibility still go to the slider.
    CHECK(root->accessibility_action({.kind = detail::AccessibilityAction::Kind::decrement}));
    CHECK(value() == 9.5f);
}

TEST_CASE("a slider's style sees its fraction", "[slider][style]") {
    detail::Scope scope;
    detail::Scope::Use use(scope);
    struct Watching final : SliderStyle {
        explicit Watching(std::vector<float>* fractions) : seen(fractions) {}
        std::vector<float>* seen;
        View make_track(const SliderConfiguration& slider) const override {
            return Rectangle()
                .frame({.height = 4})
                .opacity([seen = seen, fraction = slider.fraction] {
                    seen->push_back(fraction());
                    return 1.f;
                });
        }
        View make_thumb(const SliderConfiguration&) const override { return Rectangle().frame(10, 10); }
    };
    std::vector<float> seen;
    const State<float> value(0.25f);
    const auto root = mount(Slider(value.binding()).slider_style(Watching(&seen)), {100, 20});
    detail::flush_effects();
    REQUIRE_FALSE(seen.empty());
    CHECK(seen.back() == 0.25f);
    value = 0.75f;
    detail::flush_effects();
    CHECK(seen.back() == 0.75f);
}

TEST_CASE("the default slider style builds", "[slider][style]") {
    detail::Scope scope;
    detail::Scope::Use use(scope);
    const State<float> value(0.5f);
    CHECK_NOTHROW(mount(Slider(value.binding()), {160, 30}));
    CHECK_NOTHROW(mount(Slider(value.binding()).slider_style(KnobSliderStyle{{.track_thickness = 2, .knob_size = 14}})
                            .disabled(),
                        {160, 30}));
}
