// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// The C API's handling of what it's given: closures with a null `call`, closures and props that a
// call doesn't use, and failures. Every closure must be dropped exactly once. Under ASan, the
// views and handles the calls consume are checked for leaks too.

#include "shim.h"

#include "core/app_errors.h"
#include "text/text_system.h"
#include "view/view_node.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <vector>

using elg::Animation;
using elg::App;
using elg::AppCode;
using elg::Binding;
using elg::ErrorAction;
using elg::View;
namespace detail = elg::detail;

extern "C" {
int32_t elg_take_error(ElgStrSink out, void* ctx);
uint32_t elg_abi_version();
View* elg_empty_view();
App* elg_app_new();
void elg_app_free(App* app);
void elg_app_on_error(App* app, ElgClosure handler);
View* elg_view_component(ElgClosure body);
void elg_view_free(View* view);
View* elg_vstack(int32_t alignment, ElgOptLen spacing, View** children, size_t count);
View* elg_button(View* take_label, ElgClosure on_click);
View* elg_button_title(ElgProp title, ElgClosure on_click);
View* elg_if(ElgProp condition, View* take_then, View* take_else);
View* elg_menu_button(View* take_label, const ElgProp* title, ElgClosure menu, const ElgFloatingPlacement* placement,
                      ElgProp shows_indicator, ElgPopupMotion popup_motion);
Animation* elg_animation_custom(ElgClosure curve, double duration);
double elg_animation_progress(const Animation* animation, double seconds);
void elg_animation_free(Animation* animation);
Binding<bool>* elg_binding_bool(ElgClosure get, ElgClosure set);
Binding<int>* elg_binding_i32(ElgClosure get, ElgClosure set);
Binding<std::string>* elg_binding_string(ElgClosure get, ElgClosure set);
Binding<double>* elg_binding_f64(ElgClosure get, ElgClosure set);
void elg_binding_bool_free(Binding<bool>* binding);
void elg_binding_i32_free(Binding<int>* binding);
void elg_binding_string_free(Binding<std::string>* binding);
void elg_binding_f64_free(Binding<double>* binding);
}

namespace {

struct Counts {
    int calls = 0;
    int drops = 0;
};

bool count_call(void* data, void*, void*) {
    ++static_cast<Counts*>(data)->calls;
    return true;
}

bool panic(void* data, void*, void*) {
    ++static_cast<Counts*>(data)->calls;
    return false;
}

void count_drop(void* data) {
    ++static_cast<Counts*>(data)->drops;
}

ElgClosure closure(Counts& counts) {
    return {&counts, count_call, count_drop};
}

// No closure (`call` is null), but one the binding still expects to be dropped.
ElgClosure none(Counts& counts) {
    return {&counts, nullptr, count_drop};
}

ElgClosure panicking(Counts& counts) {
    return {&counts, panic, count_drop};
}

const bool kTrue = true;

// A static prop (kind 0) that still carries a closure.
ElgProp static_bool(Counts& counts) {
    return {.kind = 0, .theme = 0, .value = &kTrue, .fn = closure(counts)};
}

// The calling thread's error kind, cleared (0: none).
int32_t take_error() {
    return elg_take_error([](void*, const char*, size_t) {}, nullptr);
}

} // namespace

TEST_CASE("a required closure that is none is a usage error, and is still dropped", "[c-api]") {
    (void)take_error();
    Counts on_click;
    CHECK(elg_button(elg_empty_view(), none(on_click)) == nullptr);
    CHECK(take_error() == 2);
    CHECK(on_click.drops == 1);

    Counts title;
    Counts on_click_2;
    CHECK(elg_button_title({.kind = 1, .theme = 0, .value = nullptr, .fn = closure(title)}, none(on_click_2)) == nullptr);
    CHECK(take_error() == 2);
    CHECK(title.drops == 1); // the other closure, too
    CHECK(on_click_2.drops == 1);

    Counts curve;
    CHECK(elg_animation_custom(none(curve), 1) == nullptr);
    CHECK(take_error() == 2);
    CHECK(curve.drops == 1);
}

TEST_CASE("a closure a view keeps is dropped once, with the view", "[c-api]") {
    (void)take_error();
    Counts on_click;
    View* button = elg_button(elg_empty_view(), closure(on_click));
    REQUIRE(button);
    CHECK(on_click.drops == 0);
    elg_view_free(button);
    CHECK(on_click.drops == 1);
    CHECK(on_click.calls == 0);
}

TEST_CASE("a prop's closure is dropped even when the prop's kind doesn't use it", "[c-api]") {
    (void)take_error();
    Counts condition;
    View* view = elg_if(static_bool(condition), elg_empty_view(), elg_empty_view());
    REQUIRE(view);
    CHECK(condition.drops == 1);
    CHECK(condition.calls == 0);
    elg_view_free(view);
    CHECK(condition.drops == 1);
}

TEST_CASE("a menu button without a label or a title is a usage error, and drops its closures", "[c-api]") {
    (void)take_error();
    Counts menu;
    Counts indicator;
    const ElgFloatingPlacement placement{};
    CHECK(elg_menu_button(nullptr, nullptr, closure(menu), &placement, static_bool(indicator), ElgPopupMotion{}) == nullptr);
    CHECK(take_error() == 2);
    CHECK(menu.drops == 1);
    CHECK(indicator.drops == 1);
}

TEST_CASE("a stack consumes its children", "[c-api]") {
    (void)take_error();
    Counts on_click;
    View* children[] = {elg_empty_view(), elg_button(elg_empty_view(), closure(on_click))};
    View* stack = elg_vstack(0, ElgOptLen{}, children, 2);
    REQUIRE(stack);
    elg_view_free(stack);
    CHECK(on_click.drops == 1);
}

TEST_CASE("a panic in a custom curve is the call's error, not the end of the process", "[c-api]") {
    (void)take_error();
    Counts curve;
    Animation* animation = elg_animation_custom(panicking(curve), 1);
    REQUIRE(animation);
    CHECK(elg_animation_progress(animation, 0.5) == 1.0); // the documented fallback
    CHECK(take_error() == 1);
    CHECK(curve.calls == 1);
    elg_animation_free(animation);
    CHECK(curve.drops == 1);
}

TEST_CASE("bindings of every type can be freed, which drops their closures", "[c-api]") {
    (void)take_error();
    Counts get;
    Counts set;
    elg_binding_bool_free(elg_binding_bool(closure(get), closure(set)));
    elg_binding_i32_free(elg_binding_i32(closure(get), closure(set)));
    elg_binding_string_free(elg_binding_string(closure(get), closure(set)));
    elg_binding_f64_free(elg_binding_f64(closure(get), closure(set)));
    CHECK(get.drops == 4);
    CHECK(set.drops == 4);
    CHECK(take_error() == 0);
}

TEST_CASE("the C API reports its ABI version", "[c-api]") {
    CHECK(elg_abi_version() >= 1);
}

TEST_CASE("the error handler's closure is dropped once: when replaced, or with the app", "[c-api]") {
    Counts first;
    Counts second;
    App* const app = elg_app_new();
    elg_app_on_error(app, closure(first));
    elg_app_on_error(app, closure(second)); // replaces the first
    CHECK(first.drops == 1);
    elg_app_on_error(app, none(first)); // removes the handler, and drops what it was given
    CHECK(second.drops == 1);
    CHECK(first.drops == 2);
    elg_app_free(app);
    CHECK(take_error() == 0);
}

TEST_CASE("while an App runs, a panicking component body is reported, not the end of the run", "[c-api]") {
    Counts counts;
    detail::begin_app_errors();
    View* const view = elg_view_component(panicking(counts));
    REQUIRE(view);
    {
        detail::Scope scope;
        const detail::Scope::Use use(scope);
        static detail::TextSystem text; // what the placeholder of a debug build needs
        CHECK_NOTHROW(detail::lower(elg::VStack(*view), {.text = &text}));
    }
    std::vector<int> codes;
    CHECK_FALSE(detail::handle_app_errors([&](const std::exception_ptr&, AppCode code) {
        codes.push_back(static_cast<int>(code));
        return ErrorAction::resume;
    }));
    detail::end_app_errors();
    CHECK(codes == std::vector<int>{static_cast<int>(AppCode::body)});
    elg_view_free(view);
}
