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
#include "view/event_dispatcher.h"
#include "view/focus.h"
#include "view/view_node.h"

#include <elg/hot_reload.h>
#include <elg/layout.h>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <format>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <variant>
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
void elg_shape_style_assign(elg::ShapeStyle* target, const ElgShapeStyle* value);
void elg_set_badge(const ElgStr* text);
void elg_set_progress(bool has_fraction, double fraction, int32_t state);
void elg_request_attention(bool critical);
View* elg_mod_on_pointer_capture(const View* v, ElgClosure on_pointer);
View* elg_mod_on_scroll_capture(const View* v, ElgClosure on_scroll);
View* elg_mod_on_text_input_capture(const View* v, ElgClosure on_text_input);
View* elg_mod_on_key_press_capture(const View* v, int32_t key, ElgClosure on_press);
View* elg_mod_on_key_capture(const View* v, ElgClosure on_key);
View* elg_mod_rust_event_modifier(const View* v, ElgClosure make_handler);
void elg_app_menu_bar(App* app, const ElgMenuBarItem* menus, size_t count);
uint32_t elg_primary_modifier();
ElgFont elg_font_text_style(int32_t style);
int32_t elg_current_platform();
float elg_default_stack_spacing();
int32_t elg_stack_layout_spacer_axis(int32_t kind);
float elg_grid_tracks_length(const ElgGridTrack* tracks, size_t count);
bool elg_gpu_image_flipped(const ElgGpuImage* image);
void elg_effects_set_blur(elg::Effects* e, const ElgProp* radius, bool opaque);
View* elg_mod_layout_value_bool(const View* v, ElgStr name, bool value);
bool elg_layout_child_value_bool(const ElgLayoutChild* child, ElgStr name, bool* value);
View* elg_mod_layout_value_i32(const View* v, ElgStr name, int32_t value);
bool elg_layout_child_value_i32(const ElgLayoutChild* child, ElgStr name, int32_t* value);
View* elg_mod_layout_value_i64(const View* v, ElgStr name, int64_t value);
bool elg_layout_child_value_i64(const ElgLayoutChild* child, ElgStr name, int64_t* value);
View* elg_mod_layout_value_f32(const View* v, ElgStr name, float value);
bool elg_layout_child_value_f32(const ElgLayoutChild* child, ElgStr name, float* value);
View* elg_mod_layout_value_f64(const View* v, ElgStr name, double value);
bool elg_layout_child_value_f64(const ElgLayoutChild* child, ElgStr name, double* value);
View* elg_mod_layout_value_color(const View* v, ElgStr name, ElgColor value);
bool elg_layout_child_value_color(const ElgLayoutChild* child, ElgStr name, ElgColor* value);
View* elg_mod_layout_value_string(const View* v, ElgStr name, ElgStr value);
bool elg_layout_child_value_string(const ElgLayoutChild* child, ElgStr name, ElgStrSink out, void* ctx);
bool elg_env_get_named_bool(const EnvironmentValues* environment, ElgStr name, bool* value);
void elg_env_set_named_bool(EnvironmentValues* environment, ElgStr name, ElgProp value);
bool elg_env_get_named_i32(const EnvironmentValues* environment, ElgStr name, int32_t* value);
void elg_env_set_named_i32(EnvironmentValues* environment, ElgStr name, ElgProp value);
bool elg_env_get_named_i64(const EnvironmentValues* environment, ElgStr name, int64_t* value);
void elg_env_set_named_i64(EnvironmentValues* environment, ElgStr name, ElgProp value);
bool elg_env_get_named_f32(const EnvironmentValues* environment, ElgStr name, float* value);
void elg_env_set_named_f32(EnvironmentValues* environment, ElgStr name, ElgProp value);
bool elg_env_get_named_f64(const EnvironmentValues* environment, ElgStr name, double* value);
void elg_env_set_named_f64(EnvironmentValues* environment, ElgStr name, ElgProp value);
bool elg_env_get_named_color(const EnvironmentValues* environment, ElgStr name, ElgColor* value);
void elg_env_set_named_color(EnvironmentValues* environment, ElgStr name, ElgProp value);
bool elg_env_get_named_string(const EnvironmentValues* environment, ElgStr name, ElgStrSink out, void* ctx);
void elg_env_set_named_string(EnvironmentValues* environment, ElgStr name, ElgProp value);
using PropF32 = elg::Prop<float>;
using PropColor = elg::Prop<elg::Color>;
float elg_prop_f32_get(const PropF32* prop, const EnvironmentValues* environment);
PropF32* elg_prop_f32_clone(const PropF32* prop);
void elg_prop_f32_free(PropF32* prop);
ElgColor elg_prop_color_get(const PropColor* prop, const EnvironmentValues* environment);
PropColor* elg_prop_color_clone(const PropColor* prop);
void elg_prop_color_free(PropColor* prop);
PropF32* elg_effects_opacity(const elg::Effects* e);
void elg_effects_offset(const elg::Effects* e, PropF32** x, PropF32** y);
bool elg_effects_blur(const elg::Effects* e, PropF32** radius);
bool elg_effects_blur_opaque(const elg::Effects* e);
bool elg_effects_rotation(const elg::Effects* e, PropF32** degrees);
void elg_effects_scale(const elg::Effects* e, PropF32** x, PropF32** y);
bool elg_effects_z_index(const elg::Effects* e, PropF32** z);
bool elg_effects_clip_shape(const elg::Effects* e, elg::Shape** shape);
bool elg_effects_content_shape(const elg::Effects* e, elg::Shape** shape);
bool elg_effects_shadow(const elg::Effects* e, ElgShadow* shadow);
bool elg_effects_inner_shadow(const elg::Effects* e, ElgShadow* shadow);
bool elg_effects_color_filter(const elg::Effects* e, int32_t* kind, PropF32** amount, PropColor** color);
void elg_effects_add_on_size_change(elg::Effects* e, ElgClosure on_size_change);
void elg_effects_set_opacity(elg::Effects* e, ElgProp opacity);
void elg_shape_free(elg::Shape* shape);
void elg_button_style_assign(std::shared_ptr<const elg::ButtonStyle>* target, const std::shared_ptr<const elg::ButtonStyle>* style);
void elg_env_set_button_style_prop(EnvironmentValues* environment, ElgProp style);
std::shared_ptr<const elg::ButtonStyle>* elg_env_button_style(const EnvironmentValues* environment);
void elg_button_style_free(std::shared_ptr<const elg::ButtonStyle>* style);
void elg_toggle_style_assign(std::shared_ptr<const elg::ToggleStyle>* target, const std::shared_ptr<const elg::ToggleStyle>* style);
void elg_env_set_toggle_style_prop(EnvironmentValues* environment, ElgProp style);
void elg_text_field_style_assign(std::shared_ptr<const elg::TextFieldStyle>* target, const std::shared_ptr<const elg::TextFieldStyle>* style);
void elg_env_set_text_field_style_prop(EnvironmentValues* environment, ElgProp style);
std::shared_ptr<const elg::TextFieldStyle>* elg_env_text_field_style(const EnvironmentValues* environment);
void elg_text_field_style_free(std::shared_ptr<const elg::TextFieldStyle>* style);
void elg_checkbox_style_assign(std::shared_ptr<const elg::CheckboxStyle>* target, const std::shared_ptr<const elg::CheckboxStyle>* style);
void elg_env_set_checkbox_style_prop(EnvironmentValues* environment, ElgProp style);
void elg_number_input_style_assign(std::shared_ptr<const elg::NumberInputStyle>* target, const std::shared_ptr<const elg::NumberInputStyle>* style);
void elg_env_set_number_input_style_prop(EnvironmentValues* environment, ElgProp style);
void elg_progress_style_assign(std::shared_ptr<const elg::ProgressStyle>* target, const std::shared_ptr<const elg::ProgressStyle>* style);
void elg_env_set_progress_style_prop(EnvironmentValues* environment, ElgProp style);
void elg_picker_style_assign(std::shared_ptr<const elg::PickerStyle>* target, const std::shared_ptr<const elg::PickerStyle>* style);
void elg_env_set_picker_style_prop(EnvironmentValues* environment, ElgProp style);
void elg_slider_style_assign(std::shared_ptr<const elg::SliderStyle>* target, const std::shared_ptr<const elg::SliderStyle>* style);
void elg_env_set_slider_style_prop(EnvironmentValues* environment, ElgProp style);
void elg_navigation_stack_style_assign(std::shared_ptr<const elg::NavigationStackStyle>* target, const std::shared_ptr<const elg::NavigationStackStyle>* style);
void elg_env_set_navigation_stack_style_prop(EnvironmentValues* environment, ElgProp style);
void elg_popover_style_assign(std::shared_ptr<const elg::PopoverStyle>* target, const std::shared_ptr<const elg::PopoverStyle>* style);
void elg_env_set_popover_style_prop(EnvironmentValues* environment, ElgProp style);
void elg_tooltip_style_assign(std::shared_ptr<const elg::TooltipStyle>* target, const std::shared_ptr<const elg::TooltipStyle>* style);
void elg_env_set_tooltip_style_prop(EnvironmentValues* environment, ElgProp style);
void elg_menu_style_assign(std::shared_ptr<const elg::MenuStyle>* target, const std::shared_ptr<const elg::MenuStyle>* style);
void elg_env_set_menu_style_prop(EnvironmentValues* environment, ElgProp style);
void elg_row_style_assign(std::shared_ptr<const elg::RowStyle>* target, const std::shared_ptr<const elg::RowStyle>* style);
void elg_env_set_row_style_prop(EnvironmentValues* environment, ElgProp style);
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

// A view mounted in a 100-point window, with its events.
struct EventWindow {
    detail::FocusManager focus; // outlives the tree
    std::unique_ptr<detail::Element> root;
    std::unique_ptr<detail::EventDispatcher> events;

    explicit EventWindow(const View& view) {
        root = detail::lower(view.frame(100, 100), {.text = &text_system(), .focus = &focus});
        root->size_that_fits({100.f, 100.f});
        root->place({}, {100.f, 100.f});
        events = std::make_unique<detail::EventDispatcher>(*root, focus);
    }

    void press() const {
        events->pointer_button(elg::PointerEvent::Kind::down, {50, 50}, elg::MouseButton::left, 1,
                               elg::KeyModifiers::none);
    }
};

// What a logging closure writes to, and the name it writes on a pointer down.
struct Logged {
    std::vector<std::string>* log;
    std::string name;
    int32_t phase = -1; // a Rust handler's answer to op 14; -1: it doesn't answer
};

// A pointer handler (args: ElgPointerEvent*, ret: bool*) that logs downs and consumes nothing.
bool log_pointer(void* data, void* args, void* ret) {
    const auto* logged = static_cast<const Logged*>(data);
    if (static_cast<const ElgPointerEvent*>(args)->kind == 0) {
        logged->log->push_back(logged->name);
    }
    *static_cast<bool*>(ret) = false;
    return true;
}

// A Rust event handler (args: ElgEventCall*) that logs pointer downs and answers op 14 with its phase.
bool rust_handler(void* data, void* args, void* ret) {
    const auto* logged = static_cast<const Logged*>(data);
    const auto* call = static_cast<const ElgEventCall*>(args);
    if (call->op == 14 && logged->phase >= 0) {
        *static_cast<int32_t*>(ret) = logged->phase;
    } else if (call->op == 0 && call->pointer.kind == 0) {
        logged->log->push_back(logged->name);
    }
    return true;
}

// make_handler (ret: ElgClosure*): the handler above, borrowing `data`.
bool make_rust_handler(void* data, void*, void* ret) {
    *static_cast<ElgClosure*>(ret) = {data, rust_handler, nullptr};
    return true;
}

// A style prop's closure (ret: the style's handle): writes `style` with `assign`.
template <class Style>
struct PickStyle {
    const std::shared_ptr<const Style>* style;
    void (*assign)(std::shared_ptr<const Style>*, const std::shared_ptr<const Style>*);
};

template <class Style>
bool pick_style(void* data, void*, void* ret) {
    const auto* pick = static_cast<const PickStyle<Style>*>(data);
    pick->assign(static_cast<std::shared_ptr<const Style>*>(ret), pick->style);
    return true;
}

// Sets a style that isn't the default through a static prop and a reactive one, and reads it back.
template <class Style, class Concrete>
void check_style_prop(void (*set_prop)(EnvironmentValues*, ElgProp),
                      void (*assign)(std::shared_ptr<const Style>*, const std::shared_ptr<const Style>*),
                      std::shared_ptr<const Style>* (*get)(const EnvironmentValues*),
                      void (*free_style)(std::shared_ptr<const Style>*)) {
    EnvironmentValues* environment = elg_env_new();
    REQUIRE(environment);
    const std::shared_ptr<const Style> mine = std::make_shared<const Concrete>();
    set_prop(environment, ElgProp{.kind = 0, .theme = 0, .value = &mine, .fn = {}});
    std::shared_ptr<const Style>* read = get(environment);
    REQUIRE(read);
    CHECK(read->get() == mine.get());
    free_style(read);

    PickStyle<Style> pick{&mine, assign};
    set_prop(environment, ElgProp{.kind = 1, .theme = 0, .value = nullptr, .fn = {&pick, pick_style<Style>, nullptr}});
    read = get(environment);
    REQUIRE(read);
    CHECK(read->get() == mine.get());
    free_style(read);
    elg_env_free(environment);
    CHECK(take_error() == 0);
}

// A key that a binding can share by name: its value is an int.
struct RankKey {
    using Value = int;
    static constexpr std::string_view name = "test.rank";
    static int default_value(const EnvironmentValues&) { return 0; }
};

// A static prop of `value` (kind 0).
template <class C>
ElgProp static_prop(const C& value) {
    return ElgProp{.kind = 0, .theme = 0, .value = &value, .fn = {}};
}

ElgStr name_of(std::string_view name) {
    return {name.data(), name.size()};
}

// What a custom layout read from its children through the C API.
struct ChildValues {
    bool has_rank = false;
    int32_t rank = 0;
    bool has_fraction = false;
    float fraction = 0;
    bool has_label = false;
    std::string label;
    bool flag = false;
    int64_t big = 0;
    double precise = 0;
    ElgColor tint{};
};

struct ReadingByName final : elg::Layout {
    explicit ReadingByName(std::shared_ptr<std::vector<ChildValues>> record) : seen(std::move(record)) {}
    [[nodiscard]] elg::Size size_that_fits(elg::ProposedSize, std::span<const elg::LayoutChild> children) const override {
        seen->clear();
        for (const elg::LayoutChild& child : children) {
            const ElgLayoutChild* c = elg_shim::c_children(&child);
            ChildValues values;
            values.has_rank = elg_layout_child_value_i32(c, name_of(RankKey::name), &values.rank);
            values.has_fraction = elg_layout_child_value_f32(c, name_of(RankKey::name), &values.fraction);
            values.has_label = elg_layout_child_value_string(c, name_of("test.label"),
                                                             [](void* ctx, const char* ptr, size_t len) {
                                                                 static_cast<std::string*>(ctx)->assign(ptr, len);
                                                             },
                                                             &values.label);
            (void)elg_layout_child_value_bool(c, name_of("test.flag"), &values.flag);
            (void)elg_layout_child_value_i64(c, name_of("test.big"), &values.big);
            (void)elg_layout_child_value_f64(c, name_of("test.precise"), &values.precise);
            (void)elg_layout_child_value_color(c, name_of("test.tint"), &values.tint);
            seen->push_back(values);
        }
        return {};
    }
    void place_children(elg::Rect, elg::ProposedSize, std::span<const elg::LayoutChild>) const override {}

    std::shared_ptr<std::vector<ChildValues>> seen;
};

// A C++ key whose value is an int, named like RankKey: what LayoutChild::value() reads.
struct RankLayoutKey {
    using Value = int;
    static constexpr std::string_view name = "test.rank";
    static int default_value() { return 0; }
};

// Reads RankLayoutKey with LayoutChild::value(), for values set from C.
struct ReadingByKey final : elg::Layout {
    explicit ReadingByKey(std::shared_ptr<std::vector<int>> record) : seen(std::move(record)) {}
    [[nodiscard]] elg::Size size_that_fits(elg::ProposedSize, std::span<const elg::LayoutChild> children) const override {
        seen->clear();
        for (const elg::LayoutChild& child : children) {
            seen->push_back(child.value<RankLayoutKey>());
        }
        return {};
    }
    void place_children(elg::Rect, elg::ProposedSize, std::span<const elg::LayoutChild>) const override {}

    std::shared_ptr<std::vector<int>> seen;
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

TEST_CASE("a shape style of kind 5 is a backdrop", "[c-api][backdrop]") {
    elg::ShapeStyle style(elg::Color::white);
    const ElgShapeStyle backdrop{.kind = 5, .blur = 20, .saturation = 1.8f, .brightness = -0.1f, .noise = 0.02f};
    elg_shape_style_assign(&style, &backdrop);
    CHECK(take_error() == 0);
    const auto* fill = std::get_if<elg::BackdropFill>(&style.value());
    REQUIRE(fill);
    CHECK(fill->blur == 20);
    CHECK(fill->saturation == 1.8f);
    CHECK(fill->brightness == -0.1f);
    CHECK(fill->noise == 0.02f);
}

TEST_CASE("a window description's material comes through the C API", "[c-api][window]") {
    ElgWindowDesc desc{};
    desc.title = {"Window", 6};
    desc.width = 400;
    desc.height = 300;
    desc.opacity = 1;
    desc.material = 4;
    CHECK(elg_shim::window_desc(desc).material == elg::WindowMaterial::acrylic);
    desc.material = 2;
    CHECK(elg_shim::window_desc(desc).material == elg::WindowMaterial::under_window);
    desc.material = 99;
    CHECK(elg_shim::window_desc(desc).material == elg::WindowMaterial::none);
}

TEST_CASE("the dock and taskbar functions need a running App", "[c-api][desktop]") {
    const ElgStr label{"3", 1};
    elg_set_badge(&label);
    CHECK(take_error() == 2);
    elg_set_badge(nullptr);
    CHECK(take_error() == 2);
    elg_set_progress(true, 0.5, 1);
    CHECK(take_error() == 2);
    elg_set_progress(false, 0, 0);
    CHECK(take_error() == 2);
    elg_request_attention(true);
    CHECK(take_error() == 2);
}

TEST_CASE("capture handlers through the C API hear a press before the views inside them", "[c-api][capture-phase]") {
    detail::Scope scope;
    const detail::Scope::Use use(scope);
    std::vector<std::string> log;
    const View inner = Rectangle().on_pointer([&log](const elg::PointerEvent& event) {
        if (event.kind == elg::PointerEvent::Kind::down) {
            log.emplace_back("inner");
        }
        return false;
    });
    Logged outer{&log, "outer"};
    View* capturing = elg_mod_on_pointer_capture(&inner, {&outer, log_pointer, nullptr});
    REQUIRE(capturing);
    {
        const EventWindow window(*capturing);
        window.press();
    }
    CHECK(log == std::vector<std::string>{"outer", "inner"});
    elg_view_free(capturing);

    // The other capture handlers make views too, and take their closures.
    Counts counts;
    for (View* view : {elg_mod_on_scroll_capture(&inner, closure(counts)),
                       elg_mod_on_text_input_capture(&inner, closure(counts)),
                       elg_mod_on_key_press_capture(&inner, 0, closure(counts)),
                       elg_mod_on_key_capture(&inner, closure(counts))}) {
        CHECK(view != nullptr);
        elg_view_free(view);
    }
    CHECK(take_error() == 0);
    CHECK(counts.drops == 4);
}

TEST_CASE("a Rust event handler says its phase with op 14", "[c-api][capture-phase]") {
    detail::Scope scope;
    const detail::Scope::Use use(scope);
    std::vector<std::string> log;
    const View inner = Rectangle().on_pointer([&log](const elg::PointerEvent& event) {
        if (event.kind == elg::PointerEvent::Kind::down) {
            log.emplace_back("inner");
        }
        return false;
    });
    Logged capture{&log, "capture", 0};
    Logged silent{&log, "silent"}; // doesn't answer op 14: the bubble phase
    View* capturing = elg_mod_rust_event_modifier(&inner, {&capture, make_rust_handler, nullptr});
    REQUIRE(capturing);
    View* both = elg_mod_rust_event_modifier(capturing, {&silent, make_rust_handler, nullptr});
    REQUIRE(both);
    {
        const EventWindow window(*both);
        window.press();
    }
    CHECK(log == std::vector<std::string>{"capture", "inner", "silent"});
    elg_view_free(capturing);
    elg_view_free(both);
}

TEST_CASE("menu bar items convert from the C API, with their shortcuts, roles and submenus", "[c-api][menus]") {
    Counts saved;
    Counts toggled;
    const ElgMenuBarItem file_items[] = {
        {.title = {"Save", 4},
         .action = closure(saved),
         .checked = -1,
         .enabled = true,
         .has_shortcut = true,
         .shortcut_key = static_cast<int32_t>(elg::Key::s),
         .shortcut_modifiers = static_cast<uint32_t>(elg::KeyModifiers::control | elg::KeyModifiers::shift)},
        {.checked = -1, .enabled = true}, // a separator
        {.title = {"Wrap", 4}, .checked = 1, .toggled = closure(toggled), .enabled = false},
        {.checked = -1, .enabled = true, .role = 6}, // quit
        {.title = {"Odd", 3}, .checked = -1, .enabled = true, .role = 99},
    };
    const ElgMenuBarItem menus[] = {{.title = {"File", 4}, .checked = -1, .enabled = true, .submenu = file_items,
                                     .submenu_count = std::size(file_items)}};
    std::vector<elg::MenuBarItem> converted;
    {
        elg_shim::ConsumedMenu<ElgMenuBarItem, elg::MenuBarItem> consumed(menus, std::size(menus));
        converted = consumed.take();
    }
    REQUIRE(converted.size() == 1);
    CHECK(converted[0].title == "File");
    REQUIRE(converted[0].submenu.size() == 5);
    const elg::MenuBarItem& save = converted[0].submenu[0];
    CHECK(save.shortcut == std::optional<elg::KeyboardShortcut>(elg::KeyboardShortcut{
                               elg::Key::s, elg::KeyModifiers::control | elg::KeyModifiers::shift}));
    CHECK_FALSE(save.checked.has_value());
    save.action();
    CHECK(saved.calls == 1);
    CHECK(converted[0].submenu[1].title.empty());
    const elg::MenuBarItem& wrap = converted[0].submenu[2];
    CHECK(wrap.checked == std::optional<bool>(true));
    CHECK_FALSE(wrap.enabled);
    CHECK(wrap.toggled);
    CHECK(converted[0].submenu[3].role == elg::MenuRole::quit);
    CHECK(converted[0].submenu[4].role == elg::MenuRole::none);
    converted.clear();
    CHECK(saved.drops == 1);
    CHECK(toggled.drops == 1);
}

TEST_CASE("elg_app_menu_bar takes the menus' closures, and elg_primary_modifier is the platform's", "[c-api][menus]") {
    Counts quit;
    const ElgMenuBarItem items[] = {{.title = {"Quit", 4}, .action = closure(quit), .checked = -1, .enabled = true}};
    const ElgMenuBarItem menus[] = {
        {.title = {"App", 3}, .checked = -1, .enabled = true, .submenu = items, .submenu_count = 1}};
    App* app = elg_app_new();
    REQUIRE(app);
    elg_app_menu_bar(app, menus, std::size(menus));
    CHECK(take_error() == 0);
    CHECK(quit.drops == 0); // the app keeps it
    elg_app_free(app);
    CHECK(quit.drops == 1);

    CHECK(elg_primary_modifier() == static_cast<uint32_t>(elg::primary_modifier()));
}

TEST_CASE("every style of the environment can be set to a prop through the C API", "[c-api][environment]") {
    check_style_prop<elg::ButtonStyle, elg::BorderedButtonStyle>(elg_env_set_button_style_prop, elg_button_style_assign,
        elg_env_button_style, elg_button_style_free);
    check_style_prop<elg::ToggleStyle, elg::SwitchToggleStyle>(elg_env_set_toggle_style_prop, elg_toggle_style_assign,
        elg_env_toggle_style, elg_toggle_style_free);
    check_style_prop<elg::TextFieldStyle, elg::PlainTextFieldStyle>(elg_env_set_text_field_style_prop, elg_text_field_style_assign,
        elg_env_text_field_style, elg_text_field_style_free);
    check_style_prop<elg::CheckboxStyle, elg::BoxCheckboxStyle>(elg_env_set_checkbox_style_prop, elg_checkbox_style_assign,
        elg_env_checkbox_style, elg_checkbox_style_free);
    check_style_prop<elg::NumberInputStyle, elg::PlainNumberInputStyle>(elg_env_set_number_input_style_prop, elg_number_input_style_assign,
        elg_env_number_input_style, elg_number_input_style_free);
    check_style_prop<elg::ProgressStyle, elg::CircularProgressStyle>(elg_env_set_progress_style_prop, elg_progress_style_assign,
        elg_env_progress_style, elg_progress_style_free);
    check_style_prop<elg::PickerStyle, elg::MenuPickerStyle>(elg_env_set_picker_style_prop, elg_picker_style_assign,
        elg_env_picker_style, elg_picker_style_free);
    check_style_prop<elg::SliderStyle, elg::KnobSliderStyle>(elg_env_set_slider_style_prop, elg_slider_style_assign,
        elg_env_slider_style, elg_slider_style_free);
    check_style_prop<elg::NavigationStackStyle, elg::PlainNavigationStackStyle>(elg_env_set_navigation_stack_style_prop, elg_navigation_stack_style_assign,
        elg_env_navigation_stack_style, elg_navigation_stack_style_free);
    check_style_prop<elg::PopoverStyle, elg::PlainPopoverStyle>(elg_env_set_popover_style_prop, elg_popover_style_assign,
        elg_env_popover_style, elg_popover_style_free);
    check_style_prop<elg::TooltipStyle, elg::PlainTooltipStyle>(elg_env_set_tooltip_style_prop, elg_tooltip_style_assign,
        elg_env_tooltip_style, elg_tooltip_style_free);
    check_style_prop<elg::MenuStyle, elg::PlainMenuStyle>(elg_env_set_menu_style_prop, elg_menu_style_assign,
        elg_env_menu_style, elg_menu_style_free);
    check_style_prop<elg::RowStyle, elg::PlainRowStyle>(elg_env_set_row_style_prop, elg_row_style_assign,
        elg_env_row_style, elg_row_style_free);
}

TEST_CASE("a reactive style follows its state, and a style prop without a style is a usage error", "[c-api][environment]") {
    detail::Scope scope;
    const detail::Scope::Use use(scope);
    const std::shared_ptr<const elg::PickerStyle> menu = std::make_shared<const elg::MenuPickerStyle>();
    const std::shared_ptr<const elg::PickerStyle> segmented = std::make_shared<const elg::SegmentedPickerStyle>();
    struct Choice {
        const elg::State<bool>* use_menu;
        const std::shared_ptr<const elg::PickerStyle>* menu;
        const std::shared_ptr<const elg::PickerStyle>* segmented;
    };
    const elg::State<bool> use_menu(true);
    Choice choice{&use_menu, &menu, &segmented};
    const auto choose = [](void* data, void*, void* ret) {
        const auto* c = static_cast<const Choice*>(data);
        elg_picker_style_assign(static_cast<std::shared_ptr<const elg::PickerStyle>*>(ret),
                                (*c->use_menu)() ? c->menu : c->segmented);
        return true;
    };
    EnvironmentValues* environment = elg_env_new();
    REQUIRE(environment);
    elg_env_set_picker_style_prop(environment, ElgProp{.kind = 1, .theme = 0, .value = nullptr, .fn = {&choice, choose, nullptr}});
    std::shared_ptr<const elg::PickerStyle>* read = elg_env_picker_style(environment);
    REQUIRE(read);
    CHECK(read->get() == menu.get());
    elg_picker_style_free(read);
    use_menu.set(false);
    read = elg_env_picker_style(environment);
    REQUIRE(read);
    CHECK(read->get() == segmented.get());
    elg_picker_style_free(read);

    // A theme color isn't a style; a closure that writes no style is a usage error when it's read.
    elg_env_set_picker_style_prop(environment, ElgProp{.kind = 3, .theme = 0, .value = nullptr, .fn = {}});
    CHECK(take_error() == 2);
    const auto write_nothing = [](void*, void*, void*) { return true; };
    elg_env_set_picker_style_prop(environment, ElgProp{.kind = 1, .theme = 0, .value = nullptr, .fn = {nullptr, write_nothing, nullptr}});
    CHECK(take_error() == 0);
    CHECK(elg_env_picker_style(environment) == nullptr);
    CHECK(take_error() == 2);
    elg_env_free(environment);
}

TEST_CASE("the C API reads Effects back as reactive props", "[c-api][effects]") {
    detail::Scope scope;
    const detail::Scope::Use use(scope);
    const elg::State<float> faded(0.5f);
    elg::Effects effects;
    effects.opacity = elg::Prop<float>([faded] { return faded(); });

    // A handle follows its prop, and a prop of kind 4 copies it, reactive as it is.
    PropF32* opacity = elg_effects_opacity(&effects);
    REQUIRE(opacity);
    CHECK(elg_prop_f32_get(opacity, nullptr) == 0.5f);
    elg::Effects other;
    elg_effects_set_opacity(&other, ElgProp{.kind = 4, .theme = 0, .value = opacity, .fn = {}});
    CHECK(take_error() == 0);
    faded.set(0.25f);
    CHECK(elg_prop_f32_get(opacity, nullptr) == 0.25f);
    CHECK(other.opacity.get() == 0.25f);
    PropF32* copy = elg_prop_f32_clone(opacity);
    elg_prop_f32_free(opacity);
    CHECK(elg_prop_f32_get(copy, nullptr) == 0.25f);
    elg_prop_f32_free(copy);

    // Kind 4 is for float and Color props only.
    CHECK(elg_text(ElgProp{.kind = 4, .theme = 0, .value = &effects, .fn = {}}) == nullptr);
    CHECK(take_error() == 2);

    // What isn't set reads as not set; what is, reads back.
    PropF32* prop = nullptr;
    elg::Shape* shape = nullptr;
    ElgShadow shadow{};
    CHECK_FALSE(elg_effects_blur(&effects, &prop));
    CHECK_FALSE(elg_effects_rotation(&effects, &prop));
    CHECK_FALSE(elg_effects_z_index(&effects, &prop));
    CHECK_FALSE(elg_effects_clip_shape(&effects, &shape));
    CHECK_FALSE(elg_effects_content_shape(&effects, &shape));
    CHECK_FALSE(elg_effects_shadow(&effects, &shadow));
    CHECK_FALSE(elg_effects_inner_shadow(&effects, &shadow));
    CHECK(prop == nullptr);
    CHECK(shape == nullptr);

    effects.blur = 4.f;
    effects.blur_opaque = true;
    effects.rotation = 30.f;
    effects.scale_x = 2.f;
    effects.z_index = 3.f;
    effects.offset_x = 5.f;
    effects.clip_shape = elg::Circle();
    effects.content_shape = elg::Circle();
    effects.shadow = elg::Shadow{.radius = 12, .y = 4};
    effects.inner_shadow = elg::Shadow{.radius = 2};
    REQUIRE(elg_effects_blur(&effects, &prop));
    CHECK(elg_prop_f32_get(prop, nullptr) == 4.f);
    elg_prop_f32_free(prop);
    CHECK(elg_effects_blur_opaque(&effects));
    REQUIRE(elg_effects_rotation(&effects, &prop));
    CHECK(elg_prop_f32_get(prop, nullptr) == 30.f);
    elg_prop_f32_free(prop);
    REQUIRE(elg_effects_z_index(&effects, &prop));
    CHECK(elg_prop_f32_get(prop, nullptr) == 3.f);
    elg_prop_f32_free(prop);
    PropF32* x = nullptr;
    PropF32* y = nullptr;
    elg_effects_scale(&effects, &x, &y);
    REQUIRE(x);
    CHECK(elg_prop_f32_get(x, nullptr) == 2.f);
    CHECK(y == nullptr);
    elg_prop_f32_free(x);
    elg_effects_offset(&effects, &x, &y);
    REQUIRE(x);
    REQUIRE(y);
    CHECK(elg_prop_f32_get(x, nullptr) == 5.f);
    CHECK(elg_prop_f32_get(y, nullptr) == 0.f);
    elg_prop_f32_free(x);
    elg_prop_f32_free(y);
    REQUIRE(elg_effects_clip_shape(&effects, &shape));
    elg_shape_free(shape);
    REQUIRE(elg_effects_content_shape(&effects, &shape));
    elg_shape_free(shape);
    REQUIRE(elg_effects_shadow(&effects, &shadow));
    CHECK(shadow.radius == 12);
    CHECK(shadow.y == 4);
    REQUIRE(elg_effects_inner_shadow(&effects, &shadow));
    CHECK(shadow.radius == 2);
    CHECK(take_error() == 0);
}

TEST_CASE("a color filter's color is a prop that may need the environment", "[c-api][effects]") {
    elg::Effects effects;
    int32_t kind = -1;
    PropF32* amount = nullptr;
    PropColor* color = nullptr;
    CHECK_FALSE(elg_effects_color_filter(&effects, &kind, &amount, &color));
    effects.color_filter = elg::ColorFilter{.kind = elg::ColorFilter::Kind::multiply,
                                            .amount = 0.5f,
                                            .color = elg::Prop<elg::Color>(elg::ThemeColor::accent)};
    REQUIRE(elg_effects_color_filter(&effects, &kind, &amount, &color));
    CHECK(kind == static_cast<int32_t>(elg::ColorFilter::Kind::multiply));
    CHECK(elg_prop_f32_get(amount, nullptr) == 0.5f);
    elg_prop_color_get(color, nullptr); // a theme color needs an environment
    CHECK(take_error() == 2);
    EnvironmentValues* environment = elg_env_new();
    const ElgColor accent = elg_prop_color_get(color, environment);
    CHECK(take_error() == 0);
    const elg::Color expected = environment->color(elg::ThemeColor::accent);
    CHECK(accent.r == expected.r);
    CHECK(accent.a == expected.a);
    PropColor* copy = elg_prop_color_clone(color);
    REQUIRE(copy);
    elg_prop_color_free(copy);
    elg_env_free(environment);
    elg_prop_f32_free(amount);
    elg_prop_color_free(color);
}

TEST_CASE("size handlers added through the C API run after the ones before them", "[c-api][effects]") {
    std::vector<std::string> log;
    elg::Effects effects;
    effects.on_size_change = [&log](elg::PixelSize) { log.emplace_back("first"); };
    Logged added{&log, "added"};
    const auto log_size = [](void* data, void*, void*) {
        const auto* logged = static_cast<const Logged*>(data);
        logged->log->push_back(logged->name);
        return true;
    };
    elg_effects_add_on_size_change(&effects, {&added, log_size, nullptr});
    REQUIRE(effects.on_size_change);
    effects.on_size_change(elg::PixelSize{10, 20});
    CHECK(log == std::vector<std::string>{"first", "added"});

    Counts none_given;
    elg_effects_add_on_size_change(&effects, none(none_given));
    CHECK(take_error() == 2);
    CHECK(none_given.drops == 1);
}

TEST_CASE("environment keys are shared by name and value type between C++ and the C API", "[c-api][environment]") {
    EnvironmentValues* environment = elg_env_new();
    REQUIRE(environment);
    const ElgStr rank = name_of(RankKey::name);
    int32_t number = -1;
    CHECK_FALSE(elg_env_get_named_i32(environment, rank, &number)); // unset: no default
    CHECK(number == -1);

    // Set from C, read by the C++ key type; and the other way round.
    const int32_t three = 3;
    elg_env_set_named_i32(environment, rank, static_prop(three));
    CHECK(environment->get<RankKey>() == 3);
    environment->set<RankKey>(5);
    REQUIRE(elg_env_get_named_i32(environment, rank, &number));
    CHECK(number == 5);

    // The same name with another type of value is another key.
    float fraction = -1;
    CHECK_FALSE(elg_env_get_named_f32(environment, rank, &fraction));
    const float half = 0.5f;
    elg_env_set_named_f32(environment, rank, static_prop(half));
    REQUIRE(elg_env_get_named_f32(environment, rank, &fraction));
    CHECK(fraction == 0.5f);
    CHECK(environment->get<RankKey>() == 5);

    // Every type of value.
    const bool yes = true;
    const int64_t big = int64_t{1} << 40;
    const double precise = 0.125;
    const ElgColor red{1, 0, 0, 1};
    const ElgStr hello = name_of("hello");
    elg_env_set_named_bool(environment, name_of("test.flag"), static_prop(yes));
    elg_env_set_named_i64(environment, name_of("test.big"), static_prop(big));
    elg_env_set_named_f64(environment, name_of("test.precise"), static_prop(precise));
    elg_env_set_named_color(environment, name_of("test.tint"), static_prop(red));
    elg_env_set_named_string(environment, name_of("test.greeting"), static_prop(hello));
    CHECK(take_error() == 0);
    bool flag = false;
    int64_t big_read = 0;
    double precise_read = 0;
    ElgColor tint{};
    std::string greeting;
    CHECK(elg_env_get_named_bool(environment, name_of("test.flag"), &flag));
    CHECK(flag);
    CHECK(elg_env_get_named_i64(environment, name_of("test.big"), &big_read));
    CHECK(big_read == big);
    CHECK(elg_env_get_named_f64(environment, name_of("test.precise"), &precise_read));
    CHECK(precise_read == 0.125);
    CHECK(elg_env_get_named_color(environment, name_of("test.tint"), &tint));
    CHECK(tint.r == 1.f);
    CHECK(elg_env_get_named_string(environment, name_of("test.greeting"),
                                   [](void* ctx, const char* ptr, size_t len) {
                                       static_cast<std::string*>(ctx)->assign(ptr, len);
                                   },
                                   &greeting));
    CHECK(greeting == "hello");
    CHECK_FALSE(elg_env_get_named_string(environment, name_of("test.missing"), [](void*, const char*, size_t) {}, nullptr));
    elg_env_free(environment);
}

TEST_CASE("layout values are shared by name and value type between C++ and the C API", "[c-api][layout]") {
    detail::Scope scope;
    const detail::Scope::Use use(scope);
    const View base = Rectangle();
    View* ranked = elg_mod_layout_value_i32(&base, name_of(RankKey::name), 3);
    View* fractional = elg_mod_layout_value_f32(&base, name_of(RankKey::name), 0.5f);
    View* labeled = elg_mod_layout_value_string(&base, name_of("test.label"), name_of("gold"));
    View* flagged = elg_mod_layout_value_bool(&base, name_of("test.flag"), true);
    View* big = elg_mod_layout_value_i64(flagged, name_of("test.big"), int64_t{1} << 40);
    View* precise = elg_mod_layout_value_f64(big, name_of("test.precise"), 0.125);
    View* tinted = elg_mod_layout_value_color(precise, name_of("test.tint"), ElgColor{0, 1, 0, 1});
    for (View* view : {ranked, fractional, labeled, flagged, big, precise, tinted}) {
        REQUIRE(view);
    }

    // C reads what C and C++ set; another type of value under the same name reads as unset.
    auto seen = std::make_shared<std::vector<ChildValues>>();
    const auto root = detail::lower(ReadingByName(seen)(*ranked, *fractional, *labeled, *tinted,
                                                        Rectangle().layout_value<RankLayoutKey>(9)),
                                    {});
    (void)root->size_that_fits({100.f, 100.f});
    REQUIRE(seen->size() == 5);
    CHECK((*seen)[0].has_rank);
    CHECK((*seen)[0].rank == 3);
    CHECK_FALSE((*seen)[0].has_fraction);
    CHECK_FALSE((*seen)[1].has_rank);
    CHECK((*seen)[1].fraction == 0.5f);
    CHECK((*seen)[2].label == "gold");
    CHECK((*seen)[3].flag);
    CHECK((*seen)[3].big == int64_t{1} << 40);
    CHECK((*seen)[3].precise == 0.125);
    CHECK((*seen)[3].tint.g == 1.f);
    CHECK((*seen)[4].rank == 9);

    // C++ reads a value set from C with a key type of that name and value type.
    auto ranks = std::make_shared<std::vector<int>>();
    const auto by_key = detail::lower(ReadingByKey(ranks)(*ranked, *fractional), {});
    (void)by_key->size_that_fits({100.f, 100.f});
    CHECK(*ranks == std::vector<int>{3, 0});

    for (View* view : {ranked, fractional, labeled, flagged, big, precise, tinted}) {
        elg_view_free(view);
    }
    CHECK(take_error() == 0);
}

TEST_CASE("the C API gives the font text styles, the platform, and the stacks' constants", "[c-api]") {
    const elg::Font* const styles[] = {&elg::Font::large_title, &elg::Font::title,    &elg::Font::title2,
                                       &elg::Font::title3,      &elg::Font::headline, &elg::Font::body,
                                       &elg::Font::callout,     &elg::Font::subheadline, &elg::Font::footnote,
                                       &elg::Font::caption,     &elg::Font::caption2};
    for (int32_t i = 0; i < static_cast<int32_t>(std::size(styles)); ++i) {
        const ElgFont font = elg_font_text_style(i);
        CHECK(font.size == styles[i]->size());
        CHECK(font.weight == static_cast<int32_t>(styles[i]->weight()));
        CHECK(font.italic == styles[i]->is_italic());
        CHECK(font.family.len == styles[i]->family().size());
    }
    CHECK(elg_font_text_style(99).size == elg::Font::body.size());

    CHECK(elg_current_platform() == static_cast<int32_t>(elg::current_platform()));
    CHECK(elg_default_stack_spacing() == elg::default_stack_spacing);
    CHECK(elg_stack_layout_spacer_axis(0) == static_cast<int32_t>(elg::Axis::vertical));
    CHECK(elg_stack_layout_spacer_axis(1) == static_cast<int32_t>(elg::Axis::horizontal));
    CHECK(elg_stack_layout_spacer_axis(2) == -1);
    CHECK_FALSE(elg_gpu_image_flipped(nullptr));
    CHECK(take_error() == 0);
}

TEST_CASE("the C API measures grid tracks as resolve_grid_tracks lays them out", "[c-api][grid]") {
    const ElgGridTrack tracks[] = {{.size = 40, .spacing = 8}, {.size = 60, .spacing = 4}, {.size = 20, .spacing = 8}};
    const std::vector<elg::GridTrack> cpp = {{.size = 40, .spacing = 8}, {.size = 60, .spacing = 4}, {.size = 20, .spacing = 8}};
    CHECK(elg_grid_tracks_length(tracks, std::size(tracks)) == elg::grid_tracks_length(cpp));
    CHECK(elg_grid_tracks_length(nullptr, 0) == 0);
}

TEST_CASE("a float prop of kind 3 is a theme blur", "[c-api][effects]") {
    elg::Effects effects;
    const ElgProp blur{.kind = 3, .theme = static_cast<int32_t>(elg::ThemeBlur::xl), .value = nullptr, .fn = {}};
    elg_effects_set_blur(&effects, &blur, false);
    REQUIRE(take_error() == 0);
    REQUIRE(effects.blur.has_value());
    const EnvironmentValues environment;
    if (effects.blur) {
        CHECK(effects.blur->get(environment) == elg::detail::resolve_theme_blur(environment, elg::ThemeBlur::xl));
    }
}
