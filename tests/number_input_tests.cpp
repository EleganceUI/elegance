// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// NumberInput: typing applies on Enter or blur (or live), steps clamp, and it reads as a spin button.

#include "text/text_system.h"
#include "view/focus.h"
#include "view/text_field.h"
#include "view/view_node.h"

#include <elg/views.h>

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace elg;

namespace {

struct Mounted {
    detail::FocusManager focus;
    std::unique_ptr<detail::Element> root;
    detail::Element* field = nullptr;

    // The arrows are icons, which need a text system.
    explicit Mounted(const View& view) : root(detail::lower(view, {.text = &text_system(), .focus = &focus})) {
        field = find_field(*root);
        REQUIRE(field);
    }

    static detail::TextSystem& text_system() {
        static detail::TextSystem system;
        return system;
    }

    static detail::Element* find_field(detail::Element& element) {
        if (dynamic_cast<detail::TextFieldElement*>(&element)) {
            return &element;
        }
        detail::Element* found = nullptr;
        element.for_each_child([&](detail::Element& child) {
            if (!found) {
                found = find_field(child);
            }
        });
        return found;
    }

    detail::AccessibilityInfo info() const {
        detail::AccessibilityInfo out;
        root->accessibility(out);
        return out;
    }
    void act(detail::AccessibilityAction::Kind kind, double number = 0) const {
        root->accessibility_action({.kind = kind, .number = number});
        detail::flush_effects();
    }

    void focus_field() {
        focus.focus(field, false);
        detail::flush_effects();
    }
    void blur() {
        focus.focus(nullptr, false);
        detail::flush_effects();
    }
    std::string text() const {
        detail::AccessibilityInfo out;
        field->accessibility(out);
        return out.value;
    }
    void type(std::string text) {
        // Select everything first, so the typing replaces it.
        press(Key::a, primary_modifier());
        field->event_handler()->text_input({.kind = TextInputEvent::Kind::commit, .text = std::move(text)});
        detail::flush_effects();
    }
    void press(Key key, KeyModifiers modifiers = KeyModifiers::none) {
        field->event_handler()->key({.kind = KeyEvent::Kind::down, .key = key, .modifiers = modifiers});
        detail::flush_effects();
    }
};

} // namespace

TEST_CASE("a number input reads as a spin button with its range", "[number-input]") {
    detail::Scope scope;
    detail::Scope::Use use(scope);
    State<int> guests(2);
    const Mounted input(NumberInput(guests, {.min = 1, .max = 8}));
    const detail::AccessibilityInfo info = input.info();
    CHECK(info.role == detail::AccessRole::stepper);
    CHECK(info.numeric == 2);
    CHECK(info.minimum == 1);
    CHECK(info.maximum == 8);
    CHECK(info.step == 1);
    CHECK(input.text() == "2");
}

TEST_CASE("a number input steps and clamps to its bounds", "[number-input]") {
    detail::Scope scope;
    detail::Scope::Use use(scope);
    State<int> guests(7);
    const Mounted input(NumberInput(guests, {.min = 1, .max = 8}));
    input.act(detail::AccessibilityAction::Kind::increment);
    CHECK(guests() == 8);
    input.act(detail::AccessibilityAction::Kind::increment);
    CHECK(guests() == 8);
    input.act(detail::AccessibilityAction::Kind::set_numeric, -40);
    CHECK(guests() == 1);
    input.act(detail::AccessibilityAction::Kind::set_numeric, 4.6); // an int rounds
    CHECK(guests() == 5);
    CHECK(input.text() == "5");
}

TEST_CASE("typing applies on Enter and on blur, and text that isn't a number reverts", "[number-input]") {
    detail::Scope scope;
    detail::Scope::Use use(scope);
    State<double> amount(1.5);
    Mounted input(NumberInput(amount, {.max = 100}));
    input.focus_field();

    input.type("42");
    CHECK(amount() == 1.5); // not yet
    input.press(Key::enter);
    CHECK(amount() == 42);

    input.type("250");
    input.blur();
    CHECK(amount() == 100);
    CHECK(input.text() == "100");

    input.focus_field();
    input.type("lots");
    input.blur();
    CHECK(amount() == 100);
    CHECK(input.text() == "100");
}

TEST_CASE("a live number input applies valid numbers as they're typed", "[number-input]") {
    detail::Scope scope;
    detail::Scope::Use use(scope);
    State<double> amount(0);
    Mounted input(NumberInput(amount, {.min = 0, .max = 10, .live = true}));
    input.focus_field();
    input.type("7");
    CHECK(amount() == 7);
    input.type("70"); // out of range: waits for the commit, which clamps
    CHECK(amount() == 7);
    input.blur();
    CHECK(amount() == 10);
}

TEST_CASE("Up and Down step from what's typed, Shift by ten", "[number-input]") {
    detail::Scope scope;
    detail::Scope::Use use(scope);
    State<float> size(12);
    Mounted input(NumberInput(size, {.step = 0.5}));
    input.focus_field();
    input.press(Key::up);
    CHECK(size() == 12.5f);
    input.press(Key::down, KeyModifiers::shift);
    CHECK(size() == 7.5f);
    input.type("3");
    input.press(Key::up);
    CHECK(size() == 3.5f);
    CHECK(input.text() == "3.5");
}

TEST_CASE("a number input follows its value while it isn't being edited", "[number-input]") {
    detail::Scope scope;
    detail::Scope::Use use(scope);
    State<double> amount(1);
    Mounted input(NumberInput(amount, {.decimals = 2}));
    CHECK(input.text() == "1.00");
    amount = 2.5;
    detail::flush_effects();
    CHECK(input.text() == "2.50");

    input.focus_field();
    input.type("9");
    amount = 3;
    detail::flush_effects();
    CHECK(input.text() == "9"); // the edit isn't overwritten
}
