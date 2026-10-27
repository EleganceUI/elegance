// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Toggle and Checkbox: one control with two styles and roles; a check box can be mixed.

#include "text/text_system.h"
#include "view/accessibility.h"
#include "view/view_node.h"

#include <elg/shapes.h>
#include <elg/styles.h>
#include <elg/views.h>

#include <catch2/catch_test_macros.hpp>

using namespace elg;

namespace {

struct Mounted {
    std::unique_ptr<detail::Element> root;

    // The check box's check mark is an icon, which needs a text system.
    explicit Mounted(const View& view) : root(detail::lower(view, {.text = &text_system()})) {}

    static detail::TextSystem& text_system() {
        static detail::TextSystem system;
        return system;
    }

    // Toggle and Checkbox mount as their accessibility wrapper around the button.
    const detail::AccessibilitySpec& spec() const {
        return static_cast<const detail::AccessibilityElement&>(*root).spec();
    }
    std::optional<detail::ToggleState> state() const { return spec().toggled(); }
    void click() const {
        root->for_each_child([](detail::Element& button) { button.accessibility_action({}); });
        detail::flush_effects();
    }
};

const View kLabel = Rectangle().frame(10, 10);

} // namespace

TEST_CASE("a toggle flips and reads as a switch", "[toggle]") {
    detail::Scope scope;
    detail::Scope::Use use(scope);
    State<bool> on(false);
    const Mounted toggle(Toggle(kLabel, on.binding()));
    CHECK(toggle.spec().role == detail::AccessRole::toggle);
    CHECK(toggle.state() == detail::ToggleState::off);
    toggle.click();
    CHECK(on());
    CHECK(toggle.state() == detail::ToggleState::on);
}

TEST_CASE("a check box reads as one, and a mixed one checks fully", "[toggle]") {
    detail::Scope scope;
    detail::Scope::Use use(scope);
    State<bool> checked(false);
    State<bool> mixed(true);
    const Mounted box(Checkbox(kLabel, checked.binding(), {.mixed = mixed}));
    CHECK(box.spec().role == detail::AccessRole::checkbox);
    CHECK(box.state() == detail::ToggleState::mixed);

    box.click();
    CHECK(checked());
    mixed = false;
    CHECK(box.state() == detail::ToggleState::on);
    box.click();
    CHECK_FALSE(checked());
}

TEST_CASE("a switch is as wide as its content unless it fills the width", "[toggle]") {
    detail::Scope scope;
    detail::Scope::Use use(scope);
    State<bool> on(false);
    const Mounted compact(Toggle(kLabel, on.binding()));
    CHECK(compact.root->size_that_fits({300.f, std::nullopt}).width == 10 + 8 + 40);

    const Mounted row(Toggle(kLabel, on.binding()).toggle_style(SwitchToggleStyle{{.fills_width = true}}));
    CHECK(row.root->size_that_fits({300.f, std::nullopt}).width == 300);

    const Mounted sized(Toggle(kLabel, on.binding()).toggle_style(SwitchToggleStyle{{.width = 60, .spacing = 4}}));
    CHECK(sized.root->size_that_fits({300.f, std::nullopt}).width == 10 + 4 + 60);
}

TEST_CASE("a check box's style sets its size", "[toggle]") {
    detail::Scope scope;
    detail::Scope::Use use(scope);
    State<bool> checked(false);
    const Mounted box(Checkbox(kLabel, checked.binding()).checkbox_style(BoxCheckboxStyle{{.size = 24, .spacing = 6}}));
    const Size size = box.root->size_that_fits({300.f, std::nullopt});
    CHECK(size.width == 24 + 6 + 10);
    CHECK(size.height == 24);
}

TEST_CASE("a toggle without a label is just its switch", "[toggle]") {
    detail::Scope scope;
    detail::Scope::Use use(scope);
    State<bool> on(false);
    const Mounted bare(Toggle(on.binding()));
    CHECK(bare.root->size_that_fits({300.f, std::nullopt}).width == 40);
    const Mounted row(Toggle(on.binding()).toggle_style(SwitchToggleStyle{{.fills_width = true}}));
    CHECK(row.root->size_that_fits({300.f, std::nullopt}).width == 300);
    bare.click();
    CHECK(on());
}

TEST_CASE("a check box without a label is just its box", "[toggle]") {
    detail::Scope scope;
    detail::Scope::Use use(scope);
    State<bool> checked(false);
    CHECK(EmptyView().is_empty());
    CHECK_FALSE(kLabel.is_empty());
    const Mounted bare(Checkbox(checked.binding()));
    CHECK(bare.root->size_that_fits({300.f, std::nullopt}).width == 18);
    bare.click();
    CHECK(checked());
}
