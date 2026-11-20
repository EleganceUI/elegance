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
#include "reactive/timers.h"
#include "test_images.h"
#include "text/text_system.h"
#include "view/element.h"
#include "view/view_node.h"

#include <elg/hot_reload.h>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

using elg::Animation;
using elg::App;
using elg::AppCode;
using elg::Binding;
using elg::ColorScheme;
using elg::EnvironmentValues;
using elg::ErrorAction;
using elg::ModuleEntry;
using elg::Rectangle;
using elg::Text;
using elg::Timer;
using elg::Transition;
using elg::View;
namespace detail = elg::detail;

// elg_rust_module_entry()'s preview, as views.cpp declares it (ElgPreviewDesc).
struct PreviewDesc {
    ElgStr name;
    ElgStr file;
    int32_t line;
};

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
const ModuleEntry* elg_rust_module_entry(bool has_root, const PreviewDesc* previews, size_t count,
                                         bool (*build)(int32_t index, View* ret), uint64_t app_hash);
View* elg_mod_on_appear(const View* v, ElgClosure on_appear);
View* elg_mod_on_disappear(const View* v, ElgClosure on_disappear);
Text* elg_text_selectable(const Text* text, ElgProp selectable);
EnvironmentValues* elg_env_new();
void elg_env_free(EnvironmentValues* environment);
int32_t elg_env_color_scheme(const EnvironmentValues* environment);
bool elg_system_accent_color(ElgColor* color);
Animation* elg_animation_linear(double duration);
Animation* elg_animation_timing_curve(double x1, double y1, double x2, double y2, double duration);
Animation* elg_animation_repeat_count(const Animation* animation, int32_t count, bool autoreverses);
Animation* elg_animation_repeat_forever(const Animation* animation, bool autoreverses);
bool elg_animation_finished(const Animation* animation, double seconds);
struct ElgPose {
    float opacity, scale, x, y, anchor_x, anchor_y;
};
Transition* elg_transition_scale(float from, ElgAlignment anchor);
ElgPose elg_transition_pose(const Transition* transition, float progress, bool removal, ElgSize view_size);
void elg_transition_free(Transition* transition);
Timer* elg_timer_after(double seconds, ElgClosure action);
Timer* elg_timer_every(double seconds, ElgClosure action);
void elg_timer_cancel(const Timer* timer);
bool elg_timer_active(const Timer* timer);
Timer* elg_timer_clone(const Timer* timer);
void elg_timer_free(Timer* timer);
std::shared_ptr<const elg::CheckboxStyle>* elg_env_checkbox_style(const EnvironmentValues* environment);
void elg_env_set_checkbox_style(EnvironmentValues* environment, const std::shared_ptr<const elg::CheckboxStyle>* style);
void elg_checkbox_style_free(std::shared_ptr<const elg::CheckboxStyle>* style);
std::shared_ptr<const elg::NumberInputStyle>* elg_env_number_input_style(const EnvironmentValues* environment);
void elg_env_set_number_input_style(EnvironmentValues* environment, const std::shared_ptr<const elg::NumberInputStyle>* style);
void elg_number_input_style_free(std::shared_ptr<const elg::NumberInputStyle>* style);
std::shared_ptr<const elg::ProgressStyle>* elg_env_progress_style(const EnvironmentValues* environment);
void elg_env_set_progress_style(EnvironmentValues* environment, const std::shared_ptr<const elg::ProgressStyle>* style);
void elg_progress_style_free(std::shared_ptr<const elg::ProgressStyle>* style);
std::shared_ptr<const elg::PickerStyle>* elg_env_picker_style(const EnvironmentValues* environment);
void elg_env_set_picker_style(EnvironmentValues* environment, const std::shared_ptr<const elg::PickerStyle>* style);
void elg_picker_style_free(std::shared_ptr<const elg::PickerStyle>* style);
std::shared_ptr<const elg::SliderStyle>* elg_env_slider_style(const EnvironmentValues* environment);
void elg_env_set_slider_style(EnvironmentValues* environment, const std::shared_ptr<const elg::SliderStyle>* style);
void elg_slider_style_free(std::shared_ptr<const elg::SliderStyle>* style);
std::shared_ptr<const elg::NavigationStackStyle>* elg_env_navigation_stack_style(const EnvironmentValues* environment);
void elg_env_set_navigation_stack_style(EnvironmentValues* environment, const std::shared_ptr<const elg::NavigationStackStyle>* style);
void elg_navigation_stack_style_free(std::shared_ptr<const elg::NavigationStackStyle>* style);
std::shared_ptr<const elg::PopoverStyle>* elg_env_popover_style(const EnvironmentValues* environment);
void elg_env_set_popover_style(EnvironmentValues* environment, const std::shared_ptr<const elg::PopoverStyle>* style);
void elg_popover_style_free(std::shared_ptr<const elg::PopoverStyle>* style);
std::shared_ptr<const elg::TooltipStyle>* elg_env_tooltip_style(const EnvironmentValues* environment);
void elg_env_set_tooltip_style(EnvironmentValues* environment, const std::shared_ptr<const elg::TooltipStyle>* style);
void elg_tooltip_style_free(std::shared_ptr<const elg::TooltipStyle>* style);
std::shared_ptr<const elg::MenuStyle>* elg_env_menu_style(const EnvironmentValues* environment);
void elg_env_set_menu_style(EnvironmentValues* environment, const std::shared_ptr<const elg::MenuStyle>* style);
void elg_menu_style_free(std::shared_ptr<const elg::MenuStyle>* style);
std::shared_ptr<const elg::RowStyle>* elg_env_row_style(const EnvironmentValues* environment);
void elg_env_set_row_style(EnvironmentValues* environment, const std::shared_ptr<const elg::RowStyle>* style);
void elg_row_style_free(std::shared_ptr<const elg::RowStyle>* style);
std::shared_ptr<const elg::ToggleStyle>* elg_env_toggle_style(const EnvironmentValues* environment);
void elg_toggle_style_free(std::shared_ptr<const elg::ToggleStyle>* style);
bool elg_toggle_style_animation(const std::shared_ptr<const elg::ToggleStyle>* style, Animation** animation);
bool elg_checkbox_style_animation(const std::shared_ptr<const elg::CheckboxStyle>* style, Animation** animation);
bool elg_popover_style_motion(const std::shared_ptr<const elg::PopoverStyle>* style, Transition** transition, Animation** animation);
bool elg_tooltip_style_motion(const std::shared_ptr<const elg::TooltipStyle>* style, Transition** transition, Animation** animation);
bool elg_menu_style_motion(const std::shared_ptr<const elg::MenuStyle>* style, Transition** transition, Animation** animation);
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

detail::TextSystem& text_system() {
    static detail::TextSystem system;
    return system;
}

// What a view that is a Text shows.
std::string text_of(const View& view) {
    detail::Scope scope;
    const detail::Scope::Use use(scope);
    const auto element = detail::lower(view, {.text = &text_system()});
    detail::Inspection inspection;
    element->inspect(inspection);
    return inspection.detail;
}

// Two Rust modules' build functions (ElgModuleBuild), each naming its module and the view's index.
bool build_first(int32_t index, View* ret) {
    *ret = Text(std::format("first {}", index));
    return true;
}

bool build_second(int32_t index, View* ret) {
    *ret = Text(std::format("second {}", index));
    return true;
}

// Sets a style that isn't the default in a new environment, and reads it back.
template <class Style, class Concrete>
void check_env_style(std::shared_ptr<const Style>* (*get)(const EnvironmentValues*),
                     void (*set)(EnvironmentValues*, const std::shared_ptr<const Style>*),
                     void (*free_style)(std::shared_ptr<const Style>*)) {
    EnvironmentValues* environment = elg_env_new();
    REQUIRE(environment);
    const std::shared_ptr<const Style> mine = std::make_shared<const Concrete>();
    set(environment, &mine);
    std::shared_ptr<const Style>* read = get(environment);
    REQUIRE(read);
    CHECK(read->get() == mine.get());
    free_style(read);
    elg_env_free(environment);
    CHECK(take_error() == 0);
}

// A popover style that opens with a fade.
class FadingPopoverStyle final : public elg::PopoverStyle {
public:
    [[nodiscard]] View make_body(const elg::PopoverConfiguration& popover) const override { return popover.content; }
    [[nodiscard]] elg::PopupMotion motion() const override {
        return {Transition::opacity(), Animation::linear(0.2)};
    }
};

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

TEST_CASE("each Rust module's entry builds that module's views, however many previews it has", "[c-api][hot-reload]") {
    std::vector<std::string> names;
    names.reserve(300);
    for (int i = 0; i < 300; ++i) {
        names.push_back(std::format("preview {}", i));
    }
    std::vector<PreviewDesc> previews;
    previews.reserve(names.size());
    for (const std::string& name : names) {
        previews.push_back({.name = {name.data(), name.size()}, .file = {"views.rs", 8}, .line = 1});
    }
    const ModuleEntry* first = elg_rust_module_entry(true, previews.data(), previews.size(), build_first, 0);
    const ModuleEntry* second = elg_rust_module_entry(false, previews.data(), 1, build_second, 7);
    REQUIRE(first);
    REQUIRE(second);
    CHECK(first != second);
    CHECK(first->elegance_hash == elg::detail::headers_hash);
    CHECK(first->app_hash == 0);
    CHECK(second->app_hash == 7);
    CHECK(first->has_root);
    CHECK_FALSE(second->has_root);
    REQUIRE(first->previews->size() == 300);
    CHECK(std::string((*first->previews)[299].name) == "preview 299");
    CHECK(second->previews->size() == 1);
    // The module registered first still builds its own views, after the second registered.
    CHECK(text_of(first->build(first->context, -1)) == "first -1");
    CHECK(text_of(first->build(first->context, 299)) == "first 299");
    CHECK(text_of(second->build(second->context, 0)) == "second 0");
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

TEST_CASE("elg_mod_on_appear and elg_mod_on_disappear run their actions once each, and take none", "[c-api][lifecycle]") {
    detail::clear_lifecycle_actions();
    Counts appear;
    Counts gone;
    {
        detail::Scope scope;
        const detail::Scope::Use use(scope);
        const View base = Rectangle();
        View* appearing = elg_mod_on_appear(&base, closure(appear));
        REQUIRE(appearing);
        View* both = elg_mod_on_disappear(appearing, closure(gone));
        REQUIRE(both);
        elg_view_free(appearing);
        const elg::State<bool> shown(true);
        const auto root = detail::lower(elg::VStack(elg::If([shown] { return shown(); }, *both)), {});
        elg_view_free(both);
        CHECK(detail::run_lifecycle_actions());
        CHECK(appear.calls == 1);
        CHECK(gone.calls == 0);
        shown.set(false);
        detail::flush_effects();
        detail::run_lifecycle_actions();
        CHECK(gone.calls == 1);
    }
    CHECK(appear.drops == 1);
    CHECK(gone.drops == 1);

    // None: no action, and the closure is still dropped.
    Counts unused;
    const View base = Rectangle();
    View* plain = elg_mod_on_appear(&base, none(unused));
    REQUIRE(plain);
    CHECK(take_error() == 0);
    elg_view_free(plain);
    CHECK(unused.drops == 1);
    detail::clear_lifecycle_actions();
}

TEST_CASE("elg_text_selectable keeps the text and takes its prop", "[c-api][selectable-text]") {
    Counts counts;
    const Text text("hello");
    Text* selectable = elg_text_selectable(&text, static_bool(counts));
    REQUIRE(selectable);
    CHECK(take_error() == 0);
    CHECK(text_of(*selectable) == "hello");
    elg_text_free(selectable);
    CHECK(counts.drops == 1);
}

TEST_CASE("elg_env_new is an empty environment, and elg_system_accent_color doesn't fail", "[c-api][environment]") {
    EnvironmentValues* environment = elg_env_new();
    REQUIRE(environment);
    CHECK(elg_env_color_scheme(environment) == static_cast<int32_t>(ColorScheme::light));
    elg_env_free(environment);

    ElgColor color{};
    const bool has_accent = elg_system_accent_color(&color);
    CHECK(take_error() == 0);
    if (!has_accent) {
        CHECK(color.r == 0.f); // left as it was
    }
}

TEST_CASE("the C API's timing curves and repeats match the C++ API's", "[c-api][animation]") {
    Animation* curve = elg_animation_timing_curve(0.55, 0.1, 0.45, 0.9, 1.4);
    REQUIRE(curve);
    const Animation expected = Animation::timing_curve(0.55, 0.1, 0.45, 0.9, 1.4);
    for (const double t : {0.0, 0.3, 0.7, 1.4}) {
        CHECK(elg_animation_progress(curve, t) == expected.progress(t));
    }

    Animation* linear = elg_animation_linear(1);
    Animation* three = elg_animation_repeat_count(linear, 3, true);
    Animation* forever = elg_animation_repeat_forever(linear, false);
    REQUIRE(three);
    REQUIRE(forever);
    const Animation expected_three = Animation::linear(1).repeat_count(3, true);
    for (const double t : {0.5, 1.5, 2.5}) {
        CHECK(elg_animation_progress(three, t) == expected_three.progress(t));
    }
    CHECK_FALSE(elg_animation_finished(three, 2.5));
    CHECK(elg_animation_finished(three, 3.5));
    CHECK_FALSE(elg_animation_finished(forever, 1000));
    for (Animation* animation : {curve, linear, three, forever}) {
        elg_animation_free(animation);
    }
}

TEST_CASE("a transition's pose carries its anchor through the C API", "[c-api][animation]") {
    Transition* scale = elg_transition_scale(0.5f, ElgAlignment{.horizontal = 0, .vertical = 0});
    REQUIRE(scale);
    const ElgPose pose = elg_transition_pose(scale, 0, false, ElgSize{100, 100});
    CHECK(pose.scale == 0.5f);
    CHECK(pose.anchor_x == 0.f);
    CHECK(pose.anchor_y == 0.f);
    elg_transition_free(scale);
}

TEST_CASE("timers through the C API fire, cancel, and say whether they're active", "[c-api][timer]") {
    constexpr double kFarFuture = 1e12; // "now" for running every timer due by then
    detail::cancel_all_timers();
    Counts once;
    Counts repeated;
    Timer* later = elg_timer_after(1, closure(once));
    Timer* every = elg_timer_every(1, closure(repeated));
    REQUIRE(later);
    REQUIRE(every);
    CHECK(elg_timer_active(later));
    Timer* copy = elg_timer_clone(every);
    elg_timer_free(every); // freeing a handle doesn't stop the timer
    CHECK(elg_timer_active(copy));

    detail::run_due_timers(kFarFuture);
    CHECK(once.calls == 1);
    CHECK(repeated.calls == 1);
    CHECK_FALSE(elg_timer_active(later)); // fired
    elg_timer_cancel(copy);
    elg_timer_cancel(copy); // twice is fine
    CHECK_FALSE(elg_timer_active(copy));
    detail::run_due_timers(kFarFuture * 2);
    CHECK(repeated.calls == 1);
    elg_timer_free(later);
    elg_timer_free(copy);
    detail::cancel_all_timers();
    CHECK(once.drops == 1);
    CHECK(repeated.drops == 1);

    // No action: never active. NaN: a usage error, which still drops the closure.
    Counts unused;
    Timer* idle = elg_timer_after(0, none(unused));
    REQUIRE(idle);
    CHECK_FALSE(elg_timer_active(idle));
    elg_timer_free(idle);
    CHECK(unused.drops == 1);
    Counts rejected;
    CHECK(elg_timer_every(std::nan(""), closure(rejected)) == nullptr);
    CHECK(take_error() == 2);
    CHECK(rejected.drops == 1);
}

TEST_CASE("the C API reads and sets every style of the environment", "[c-api][environment]") {
    check_env_style<elg::CheckboxStyle, elg::BoxCheckboxStyle>(elg_env_checkbox_style, elg_env_set_checkbox_style,
                                                               elg_checkbox_style_free);
    check_env_style<elg::NumberInputStyle, elg::PlainNumberInputStyle>(
        elg_env_number_input_style, elg_env_set_number_input_style, elg_number_input_style_free);
    check_env_style<elg::ProgressStyle, elg::CircularProgressStyle>(elg_env_progress_style, elg_env_set_progress_style,
                                                                    elg_progress_style_free);
    check_env_style<elg::PickerStyle, elg::MenuPickerStyle>(elg_env_picker_style, elg_env_set_picker_style,
                                                            elg_picker_style_free);
    check_env_style<elg::SliderStyle, elg::KnobSliderStyle>(elg_env_slider_style, elg_env_set_slider_style,
                                                            elg_slider_style_free);
    check_env_style<elg::NavigationStackStyle, elg::PlainNavigationStackStyle>(
        elg_env_navigation_stack_style, elg_env_set_navigation_stack_style, elg_navigation_stack_style_free);
    check_env_style<elg::PopoverStyle, elg::PlainPopoverStyle>(elg_env_popover_style, elg_env_set_popover_style,
                                                               elg_popover_style_free);
    check_env_style<elg::TooltipStyle, elg::PlainTooltipStyle>(elg_env_tooltip_style, elg_env_set_tooltip_style,
                                                               elg_tooltip_style_free);
    check_env_style<elg::MenuStyle, elg::PlainMenuStyle>(elg_env_menu_style, elg_env_set_menu_style,
                                                         elg_menu_style_free);
    check_env_style<elg::RowStyle, elg::PlainRowStyle>(elg_env_row_style, elg_env_set_row_style, elg_row_style_free);
}

TEST_CASE("the C API reads the animations and motions of styles", "[c-api][environment]") {
    EnvironmentValues* environment = elg_env_new();
    REQUIRE(environment);

    std::shared_ptr<const elg::ToggleStyle>* toggle = elg_env_toggle_style(environment);
    std::shared_ptr<const elg::CheckboxStyle>* checkbox = elg_env_checkbox_style(environment);
    REQUIRE(toggle);
    REQUIRE(checkbox);
    Animation* animation = nullptr;
    const std::optional<Animation> toggle_expected = (*toggle)->animation();
    const bool toggle_animates = elg_toggle_style_animation(toggle, &animation);
    REQUIRE(toggle_animates == toggle_expected.has_value());
    if (toggle_expected) {
        CHECK(elg_animation_progress(animation, 0.1) == toggle_expected->progress(0.1));
        elg_animation_free(animation);
    }
    animation = nullptr;
    const bool checkbox_animates = elg_checkbox_style_animation(checkbox, &animation);
    REQUIRE(checkbox_animates == (*checkbox)->animation().has_value());
    if (checkbox_animates) {
        elg_animation_free(animation);
    }
    elg_toggle_style_free(toggle);
    elg_checkbox_style_free(checkbox);

    // The built-in popup styles open at once; a style with a motion gives its transition and animation.
    Transition* transition = nullptr;
    animation = nullptr;
    std::shared_ptr<const elg::TooltipStyle>* tooltip = elg_env_tooltip_style(environment);
    std::shared_ptr<const elg::MenuStyle>* menu = elg_env_menu_style(environment);
    REQUIRE(tooltip);
    REQUIRE(menu);
    CHECK_FALSE(elg_tooltip_style_motion(tooltip, &transition, &animation));
    CHECK_FALSE(elg_menu_style_motion(menu, &transition, &animation));
    CHECK(transition == nullptr);
    elg_tooltip_style_free(tooltip);
    elg_menu_style_free(menu);
    const std::shared_ptr<const elg::PopoverStyle> fading = std::make_shared<const FadingPopoverStyle>();
    REQUIRE(elg_popover_style_motion(&fading, &transition, &animation));
    REQUIRE(transition);
    REQUIRE(animation);
    CHECK(elg_animation_progress(animation, 0.1) == Animation::linear(0.2).progress(0.1));
    elg_transition_free(transition);
    elg_animation_free(animation);
    elg_env_free(environment);
    CHECK(take_error() == 0);
}
