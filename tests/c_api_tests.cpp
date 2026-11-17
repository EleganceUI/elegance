// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// The C API's handling of what it's given: closures with a null `call`, closures and props that a
// call doesn't use, and failures. Every closure must be dropped exactly once. Under ASan, the
// views and handles the calls consume are checked for leaks too.

#include "shim.h"

#include "core/app_errors.h"
#include "core/ui_tasks.h"
#include "reactive/runtime.h"
#include "test_images.h"
#include "text/text_system.h"
#include "view/view_node.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using elg::Animation;
using elg::App;
using elg::AppCode;
using elg::Binding;
using elg::ErrorAction;
using elg::Text;
using elg::View;
namespace detail = elg::detail;

extern "C" {
int32_t elg_take_error(ElgStrSink out, void* ctx);
uint32_t elg_abi_version();
const char* elg_version();
void elg_check_ui_thread();
uint64_t elg_signal_new(ElgClosure owner);
uint64_t elg_computed_new(ElgClosure compute);
void elg_node_track(uint64_t node);
void elg_node_notify(uint64_t node);
elg::VideoFeed* elg_video_feed_new();
void elg_video_feed_free(elg::VideoFeed* feed);
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
Text* elg_text(ElgProp content);
View* elg_text_to_view(const Text* text);
void elg_text_free(Text* text);
void elg_string_assign(std::string* target, ElgStr value);
View* elg_image(const ElgImageSource* source);
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

TEST_CASE("strings with malformed UTF-8 show as U+FFFD", "[c-api][utf8]") {
    elg::detail::Scope scope;
    const elg::detail::Scope::Use use(scope);
    static elg::detail::TextSystem text_system;
    const auto label = [](ElgProp content) {
        Text* text = elg_text(content);
        REQUIRE(text != nullptr);
        View* view = elg_text_to_view(text);
        elg_text_free(text);
        REQUIRE(view != nullptr);
        const std::unique_ptr<elg::detail::Element> element = elg::detail::lower(*view, {.text = &text_system});
        elg_view_free(view);
        elg::detail::AccessibilityInfo info;
        element->accessibility(info);
        return info.label;
    };
    // A static string, cut short in the middle of a character.
    const ElgStr cut{"ab\xE2\x82", 4};
    CHECK(label({.kind = 0, .theme = 0, .value = &cut, .fn = {}}) == "ab\xEF\xBF\xBD");
    // One a closure returns (through elg_string_assign), with a byte that never occurs in UTF-8.
    const ElgClosure returns_bad = {nullptr,
                                    [](void*, void*, void* ret) {
                                        elg_string_assign(static_cast<std::string*>(ret), {"\xFFok", 3});
                                        return true;
                                    },
                                    nullptr};
    CHECK(label({.kind = 1, .theme = 0, .value = nullptr, .fn = returns_bad}) == "\xEF\xBF\xBDok");
}

TEST_CASE("an image source's pixel limit comes through the C API, and 0 is the default", "[c-api][image]") {
    elg::detail::Scope scope;
    const elg::detail::Scope::Use use(scope);
    const std::vector<std::uint8_t> png = elg::test::black_png(64, 32);
    const auto width = [](const ElgImageSource& source) {
        View* view = elg_image(&source);
        REQUIRE(view != nullptr);
        const std::unique_ptr<elg::detail::Element> element = elg::detail::lower(*view, {});
        elg_view_free(view);
        return element->size_that_fits({}).width; // 0: refused
    };
    ElgImageSource source{.system_name = {}, .file = {}, .data = {png.data(), png.size()}, .scale = 1,
                          .max_pixels = 2047, .max_side = 0};
    CHECK(width(source) == 0);
    source.max_pixels = 0;
    CHECK(width(source) == 64);
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

TEST_CASE("what a call consumed but didn't take over, on another thread of a running App, is released on "
          "the UI thread",
          "[c-api][threads]") {
    detail::set_ui_thread(std::this_thread::get_id()); // as App::run does
    detail::begin_ui_releases();
    Counts button_counts;
    View* const button = elg_button(elg_empty_view(), closure(button_counts)); // made on the UI thread
    REQUIRE(button);
    Counts counts;
    std::thread([&] {
        ElgClosure unused = closure(counts);
        View* take_view = button;
        const elg_shim::Consumed consumed(unused, take_view);
    }).join();
    CHECK(counts.drops == 0); // waiting for the UI thread
    CHECK(button_counts.drops == 0);
    CHECK(detail::run_ui_releases());
    CHECK(counts.drops == 1);
    CHECK(button_counts.drops == 1); // the view went, and the action it kept
    detail::end_ui_releases();
    detail::set_ui_thread(std::nullopt);
}

TEST_CASE("while an App runs, a function of the UI thread fails on another thread, and releases what it "
          "consumed on the UI thread",
          "[c-api][threads]") {
    detail::set_ui_thread(std::this_thread::get_id()); // as App::run does
    detail::begin_ui_releases();
    // What the other thread saw: each call's result and error.
    struct Seen {
        bool view_made = true;
        int32_t view_error = 0;
        std::string view_message;
        bool button_made = true;
        int32_t button_error = 0;
        int drops_there = -1;
        int32_t check_error = 0;
        bool feed_made = false;
        bool version_read = false;
        int32_t any_thread_error = -1;
    } seen;
    const auto take = [](std::string& message) {
        return elg_take_error(
            [](void* ctx, const char* ptr, size_t len) { static_cast<std::string*>(ctx)->assign(ptr, len); }, &message);
    };
    Counts counts;
    View* const label = elg_empty_view(); // made on the UI thread
    REQUIRE(label);
    std::thread([&] {
        std::string ignored;
        seen.view_made = elg_empty_view() != nullptr;
        seen.view_error = take(seen.view_message);
        seen.button_made = elg_button(label, closure(counts)) != nullptr;
        seen.button_error = take(ignored);
        seen.drops_there = counts.drops;
        elg_check_ui_thread();
        seen.check_error = take(ignored);
        elg::VideoFeed* const feed = elg_video_feed_new(); // callable from any thread
        seen.feed_made = feed != nullptr;
        elg_video_feed_free(feed);
        seen.version_read = elg_version() != nullptr;
        seen.any_thread_error = take(ignored);
    }).join();
    CHECK_FALSE(seen.view_made);
    CHECK(seen.view_error == 2); // a usage error
    CHECK(seen.view_message.starts_with("elg_empty_view() was called off the UI thread"));
    CHECK_FALSE(seen.button_made);
    CHECK(seen.button_error == 2);
    CHECK(seen.drops_there == 0); // its closure (and label) wait for the UI thread
    CHECK(seen.check_error == 2);
    CHECK(seen.feed_made);
    CHECK(seen.version_read);
    CHECK(seen.any_thread_error == 0);
    CHECK(detail::run_ui_releases());
    CHECK(counts.drops == 1);
    detail::end_ui_releases();
    detail::set_ui_thread(std::nullopt);
}

TEST_CASE("a handle to reactive state that has ended is a usage error, also once its slot is reused",
          "[c-api][reactive]") {
    Counts first;
    Counts second;
    std::uint64_t ended = 0;
    {
        detail::Scope scope;
        const detail::Scope::Use use(scope);
        ended = elg_signal_new(closure(first));
        REQUIRE(ended != 0);
        elg_node_notify(ended);
        CHECK(take_error() == 0);
    } // the scope ends, and the node with it
    CHECK(first.drops == 1);
    elg_node_track(ended);
    CHECK(take_error() == 2);
    {
        detail::Scope scope;
        const detail::Scope::Use use(scope);
        const std::uint64_t reused = elg_signal_new(closure(second)); // most likely in the ended one's slot
        REQUIRE(reused != 0);
        CHECK(reused != ended);
        elg_node_notify(ended);
        CHECK(take_error() == 2);
        elg_node_track(reused);
        CHECK(take_error() == 0);
        const std::uint64_t computed = elg_computed_new(closure(second));
        REQUIRE(computed != 0);
        elg_node_track(computed);
        CHECK(take_error() == 0);
    }
    elg_node_track(0); // no handle at all
    CHECK(take_error() == 2);
}
