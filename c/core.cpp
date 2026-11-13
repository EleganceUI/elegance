// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Errors, view handles, reactive state, the environment, App, animations.

#include "shim.h"

#include <exception>

using namespace elg;
using namespace elg_shim;

static_assert(sizeof(Color) == sizeof(ElgColor));
static_assert(sizeof(Theme) == 34 * sizeof(ElgColor) + 7 * sizeof(float));
static_assert(sizeof(Size) == sizeof(ElgSize));
static_assert(sizeof(LayoutChild) == sizeof(void*), "Rust views a span of LayoutChild as a slice of pointers");

// --- Errors ---

namespace {

enum ErrorKind : int32_t {
    no_error = 0,
    panic_error = 1,
    usage_error = 2,
    resource_error = 3,
    gpu_error = 4,
    platform_error = 5,
    elegance_error = 6,
    other_error = 7,
};

thread_local int32_t g_error_kind = no_error;
thread_local std::string g_error_message;

} // namespace

void elg_shim::capture_error() noexcept {
    try {
        throw;
    } catch (const RustPanic&) {
        g_error_kind = panic_error;
        g_error_message = "a Rust callback panicked";
    } catch (const UsageError& e) {
        g_error_kind = usage_error;
        g_error_message = e.what();
    } catch (const ResourceError& e) {
        g_error_kind = resource_error;
        g_error_message = e.what();
    } catch (const GpuError& e) {
        g_error_kind = gpu_error;
        g_error_message = e.what();
    } catch (const PlatformError& e) {
        g_error_kind = platform_error;
        g_error_message = e.what();
    } catch (const Error& e) {
        g_error_kind = elegance_error;
        g_error_message = e.what();
    } catch (const std::exception& e) {
        g_error_kind = other_error;
        g_error_message = e.what();
    } catch (...) {
        g_error_kind = other_error;
        g_error_message = "unknown C++ exception";
    }
}

/// Takes this thread's pending error: the pending error's kind (0: none), with its message sent to
/// `out`; clears it. A call that fails records its exception here, replacing any earlier one.
///
/// @param out Receives the message, if there's an error.
/// @param ctx Passed to `out`.
/// @return The error's kind: 0 none, 1 a Rust callback panicked, 2 elg::UsageError,
///   3 elg::ResourceError, 4 elg::GpuError, 5 elg::PlatformError, 6 another elg::Error, 7 any other
///   C++ exception.
ELG_EXPORT int32_t elg_take_error(ElgStrSink out, void* ctx) {
    const int32_t kind = std::exchange(g_error_kind, no_error);
    if (kind != no_error) {
        sink(out, ctx, g_error_message);
        g_error_message.clear();
    }
    return kind;
}

/// Whether this thread has an error that elg_take_error() hasn't taken yet.
///
/// @return true if an error is pending.
ELG_EXPORT bool elg_error_pending() {
    return g_error_kind != no_error;
}

/// Sets a C++ std::string: how a closure returns a string (ret: `std::string*`), as a
/// `Binding<std::string>` getter or a string prop does.
///
/// @param target The string to set (borrowed).
/// @param value The new contents, copied.
ELG_EXPORT void elg_string_assign(std::string* target, ElgStr value) {
    target->assign(view(value));
}

/// The contents of a C++ std::string, such as a closure's `std::string*` argument.
///
/// @param s The string (borrowed).
/// @return Its bytes, valid while `s` lives and doesn't change.
ELG_EXPORT ElgStr elg_string_view(const std::string* s) {
    return c_str(*s);
}

// --- Views ---

/// Wraps `View()`: an empty view, which takes no space and draws nothing.
///
/// @return The new view, owned by the caller.
ELG_EXPORT View* elg_view_empty() {
    return new View();
}

/// Copies a view (an immutable value, cheap to copy).
///
/// @param view The view (borrowed).
/// @return The copy, owned by the caller.
ELG_EXPORT View* elg_view_clone(const View* view) {
    return new View(*view);
}

/// Frees a view.
///
/// @param view The view (consumed); may be null.
ELG_EXPORT void elg_view_free(View* view) {
    delete view;
}

/// Moves `take_view` into `*target` (a closure's View result).
///
/// @param target The closure's `ret` (borrowed); what it held is replaced.
/// @param take_view The view (consumed).
ELG_EXPORT void elg_view_move_into(View* target, View* take_view) {
    *target = std::move(*take_view);
    delete take_view;
}

/// Wraps `View::is_empty()`: whether this is an empty view (View(), EmptyView()), e.g. a style's
/// label for a control made without one.
///
/// @param view The view (borrowed).
/// @return true if it's empty.
ELG_EXPORT bool elg_view_is_empty(const View* view) {
    return view->is_empty();
}

/// A Rust component: `body` (ret: View*) runs each time the view is mounted. The C++ side of a
/// component (a type with `View body() const`).
///
/// @param body Consumed closure; args: null, ret: `View*` (set it with elg_view_move_into()).
/// @return The new view, owned by the caller, or nullptr on failure.
ELG_EXPORT View* elg_view_component(ElgClosure body) {
    ELG_TRY
    return out(View(RustComponent{share(body)}));
    ELG_CATCH(nullptr)
}

// --- Reactive state ---
//
// Rust keeps the values; each State/Computed/Binding has a C++ node for dependency tracking and
// lifetime. Nodes belong to the current Scope (the mounted view tree, If branch, ForEach item...),
// so they end with the views that made them. `owner`'s drop frees the Rust side's slot.

namespace {

using OwnerNode = detail::SignalNode<SharedFn>;

std::vector<std::shared_ptr<detail::ReactiveNode>>& roots() {
    // Leaked: nodes made outside any scope (before App::run) live for the whole program, and
    // must not be destroyed after the Rust side's thread-locals are gone.
    static auto* nodes = new std::vector<std::shared_ptr<detail::ReactiveNode>>();
    return *nodes;
}

void adopt(std::shared_ptr<detail::ReactiveNode> node) {
    if (detail::Scope* scope = detail::current_scope()) {
        scope->adopt(std::move(node));
    } else {
        roots().push_back(std::move(node));
    }
}

} // namespace

/// A signal node owning `owner`: the C++ side of a Rust State, for dependency tracking. It belongs
/// to the current Scope and ends with it; made outside any scope (before App::run), it lives for
/// the whole program.
///
/// @param owner Consumed closure that's never called; its `drop` runs when the node ends.
/// @return The node, for elg_node_track() and elg_node_notify(). Its scope owns it, not the caller.
///   nullptr on failure.
ELG_EXPORT void* elg_signal_new(ElgClosure owner) {
    ELG_TRY
    auto node = std::make_shared<OwnerNode>(std::make_shared<const RustFn>(owner));
    void* raw = static_cast<detail::ReactiveNode*>(node.get());
    adopt(std::move(node));
    return raw;
    ELG_CATCH(nullptr)
}

/// A computed node: the C++ side of a Rust Computed, owned by the current Scope like
/// elg_signal_new()'s. `compute` (ret: uint64_t*) brings the Rust value up to date and returns a
/// version that changes whenever the value does.
///
/// @param compute Consumed closure, run when the node is read and may have changed; args: null,
///   ret: `uint64_t*`. When the version stays the same, the node's dependents don't rerun.
/// @return The node, for elg_node_track(). Its scope owns it, not the caller. nullptr on failure.
ELG_EXPORT void* elg_computed_new(ElgClosure compute) {
    ELG_TRY
    const SharedFn f = std::make_shared<const RustFn>(compute);
    auto node = std::make_shared<detail::ComputedNode<std::uint64_t>>([f] {
        std::uint64_t version = 0;
        f->call(nullptr, &version);
        return version;
    });
    void* raw = static_cast<detail::ReactiveNode*>(node.get());
    adopt(std::move(node));
    return raw;
    ELG_CATCH(nullptr)
}

/// A read (`ReactiveNode::track()`): subscribes the running computed/effect, bringing a computed
/// up to date first.
///
/// @param node A node from elg_signal_new() or elg_computed_new() (borrowed).
ELG_EXPORT void elg_node_track(void* node) {
    ELG_TRY
    static_cast<detail::ReactiveNode*>(node)->track();
    ELG_CATCH_VOID
}

/// A write happened (UI thread only): `ReactiveNode::notify_changed()` marks the node's dependents
/// and schedules the effects it affects. Off the UI thread, records a UsageError instead.
///
/// @param node A node from elg_signal_new() (borrowed).
ELG_EXPORT void elg_node_notify(void* node) {
    ELG_TRY
    detail::check_ui_thread();
    static_cast<detail::ReactiveNode*>(node)->notify_changed();
    ELG_CATCH_VOID
}

/// Wraps `detail::check_ui_thread()`: records a UsageError when called off the UI thread of a
/// running App.
ELG_EXPORT void elg_check_ui_thread() {
    ELG_TRY
    detail::check_ui_thread();
    ELG_CATCH_VOID
}

/// Wraps `elg::effect()`: runs `function` now and again whenever state it read changes (after the
/// current event, before the next frame). Call it while building views; it ends when those views
/// are removed. Elsewhere it records a UsageError.
///
/// @param function Consumed closure; args and ret: null.
ELG_EXPORT void elg_effect(ElgClosure function) {
    ELG_TRY
    effect(action(function));
    ELG_CATCH_VOID
}

/// Runs `function` (borrowed) without tracking reads: `elg::untrack()`.
///
/// @param function Borrowed closure, called once before this returns; args and ret: null.
ELG_EXPORT void elg_untrack(ElgClosure function) {
    ELG_TRY
    untrack([&] { call_now(function, nullptr, nullptr); });
    ELG_CATCH_VOID
}

/// Wraps `elg::batch()`: runs `function`, grouping its writes so that effects run once, after the
/// outermost batch ends.
///
/// @param function Borrowed closure, called once before this returns; args and ret: null.
ELG_EXPORT void elg_batch(ElgClosure function) {
    ELG_TRY
    batch([&] { call_now(function, nullptr, nullptr); });
    ELG_CATCH_VOID
}

/// Wraps `elg::with_animation()`: runs `changes` in an animated transaction, so every property
/// change its writes cause animates with `animation`, including layout and If/ForEach insertions
/// and removals.
///
/// @param animation The animation (borrowed).
/// @param changes Borrowed closure, called once before this returns; args and ret: null.
ELG_EXPORT void elg_with_animation(const Animation* animation, ElgClosure changes) {
    ELG_TRY
    with_animation(*animation, [&] { call_now(changes, nullptr, nullptr); });
    ELG_CATCH_VOID
}

// --- Bindings to the C++ value types controls take ---

template <class T>
static Binding<T>* make_binding(ElgClosure get, ElgClosure set) {
    const SharedFn g = std::make_shared<const RustFn>(get);
    const SharedFn s = std::make_shared<const RustFn>(set);
    return new Binding<T>(
        [g] {
            T value{};
            g->call(nullptr, &value);
            return value;
        },
        [s](T value) { s->call(&value, nullptr); });
}

/// A `Binding<bool>` over a Rust value, for a control: `get` reads it, `set` writes it.
///
/// @param get Consumed closure; args: null, ret: `bool*`.
/// @param set Consumed closure; args: `bool*` (the new value), ret: null.
/// @return The binding, owned by the caller until a function takes it (`take_*`).
ELG_EXPORT Binding<bool>* elg_binding_bool(ElgClosure get, ElgClosure set) {
    return make_binding<bool>(get, set);
}
/// A `Binding<float>` over a Rust value, for a control: `get` reads it, `set` writes it.
///
/// @param get Consumed closure; args: null, ret: `float*`.
/// @param set Consumed closure; args: `float*` (the new value), ret: null.
/// @return The binding, owned by the caller until a function takes it (`take_*`) or
///   elg_binding_f32_free() frees it.
ELG_EXPORT Binding<float>* elg_binding_f32(ElgClosure get, ElgClosure set) {
    return make_binding<float>(get, set);
}
/// A `Binding<int>` over a Rust value, for a control: `get` reads it, `set` writes it.
///
/// @param get Consumed closure; args: null, ret: `int32_t*`.
/// @param set Consumed closure; args: `int32_t*` (the new value), ret: null.
/// @return The binding, owned by the caller until a function takes it (`take_*`).
ELG_EXPORT Binding<int>* elg_binding_i32(ElgClosure get, ElgClosure set) {
    return make_binding<int>(get, set);
}
/// A `Binding<std::string>` over a Rust value, for a control: `get` reads it, `set` writes it.
///
/// @param get Consumed closure; args: null, ret: `std::string*` (set it with elg_string_assign()).
/// @param set Consumed closure; args: `std::string*`, the new value (read it with elg_string_view());
///   ret: null.
/// @return The binding, owned by the caller until a function takes it (`take_*`).
ELG_EXPORT Binding<std::string>* elg_binding_string(ElgClosure get, ElgClosure set) {
    return make_binding<std::string>(get, set);
}
/// A `Binding<double>` over a Rust value, for a control: `get` reads it, `set` writes it.
///
/// @param get Consumed closure; args: null, ret: `double*`.
/// @param set Consumed closure; args: `double*` (the new value), ret: null.
/// @return The binding, owned by the caller until a function takes it (`take_*`).
ELG_EXPORT Binding<double>* elg_binding_f64(ElgClosure get, ElgClosure set) {
    return make_binding<double>(get, set);
}

/// Writes an optional float into a closure's std::optional<float> result.
///
/// @param target The closure's `ret` (borrowed).
/// @param value The value; `has` false: nullopt.
ELG_EXPORT void elg_opt_float_assign(std::optional<float>* target, ElgOptLen value) {
    *target = value.has ? std::optional<float>(value.value) : std::nullopt;
}
static_assert(sizeof(EdgeInsets) == sizeof(ElgEdgeInsets), "Rust writes EdgeInsets results as ElgEdgeInsets");

/// Reads a binding made by Elegance (AudioPlayer::time_binding, ...): `Binding<float>::get()`.
///
/// @param binding The binding (borrowed).
/// @return The value, or 0 on failure.
ELG_EXPORT float elg_binding_f32_get(const Binding<float>* binding) {
    ELG_TRY
    return binding->get();
    ELG_CATCH(0.f)
}
/// Writes a binding made by Elegance: `Binding<float>::set()` (an audio player's time binding
/// seeks, ...).
///
/// @param binding The binding (borrowed).
/// @param value The new value.
ELG_EXPORT void elg_binding_f32_set(const Binding<float>* binding, float value) {
    ELG_TRY
    binding->set(value);
    ELG_CATCH_VOID
}
/// Frees a `Binding<float>` the caller owns, from elg_audio_player_binding(), elg_binding_f32(), ...
///
/// @param binding The binding (consumed); may be null.
ELG_EXPORT void elg_binding_f32_free(Binding<float>* binding) {
    delete binding;
}

// --- Environment ---

namespace {

// Environment values set from Rust live in one entry: a persistent map owned by the Rust side.
struct RustEnvironmentKey {
    using Value = SharedFn;
    static constexpr std::string_view name = "rust.environment";
    static SharedFn default_value(const EnvironmentValues&) { return nullptr; }
};

} // namespace

/// A copy of the environment of the views being built (`detail::current_environment()`), as
/// `Environment<Key>()` reads it: while a body, a modifier's body, a style's make_body, or a
/// window's content function runs.
///
/// @return The environment, owned by the caller (elg_env_free()), or nullptr with a UsageError
///   when no views are being built.
ELG_EXPORT EnvironmentValues* elg_env_current() {
    ELG_TRY
    const EnvironmentValues* environment = detail::current_environment();
    if (!environment) {
        detail::throw_no_environment();
    }
    return new EnvironmentValues(*environment);
    ELG_CATCH(nullptr)
}

/// Copies an environment (cheap: copies share their entries).
///
/// @param environment The environment (borrowed).
/// @return The copy, owned by the caller.
ELG_EXPORT EnvironmentValues* elg_env_clone(const EnvironmentValues* environment) {
    return new EnvironmentValues(*environment);
}

/// Frees an environment.
///
/// @param environment The environment (consumed); may be null.
ELG_EXPORT void elg_env_free(EnvironmentValues* environment) {
    delete environment;
}

/// Wraps `EnvironmentValues::color()`: a semantic color in this environment's theme and color
/// scheme. A tracked read.
///
/// @param environment The environment (borrowed).
/// @param token An elg::ThemeColor.
/// @return The color, or all zeros on failure.
ELG_EXPORT ElgColor elg_env_color(const EnvironmentValues* environment, int32_t token) {
    ELG_TRY
    return c_color(environment->color(static_cast<ThemeColor>(token)));
    ELG_CATCH(ElgColor{})
}

/// Reads the environment's font (FontKey). A tracked read.
///
/// @param environment The environment (borrowed).
/// @param family Receives the font's family; empty for the system font.
/// @param ctx Passed to `family`.
/// @param size Receives the size in points.
/// @param weight Receives the elg::Weight.
/// @param italic Receives whether it's italic.
ELG_EXPORT void elg_env_font(const EnvironmentValues* environment, ElgStrSink family, void* ctx, float* size,
                             int32_t* weight, bool* italic) {
    ELG_TRY
    const Font font = environment->get<FontKey>();
    sink(family, ctx, font.family());
    *size = font.size();
    *weight = static_cast<int32_t>(font.weight());
    *italic = font.is_italic();
    ELG_CATCH_VOID
}

/// Reads the environment's color scheme (ColorSchemeKey). A tracked read.
///
/// @param environment The environment (borrowed).
/// @return The elg::ColorScheme: 0 light, 1 dark. 0 on failure.
ELG_EXPORT int32_t elg_env_color_scheme(const EnvironmentValues* environment) {
    ELG_TRY
    return static_cast<int32_t>(environment->get<ColorSchemeKey>());
    ELG_CATCH(0)
}

/// Reads the environment's theme (ThemeKey). A tracked read.
///
/// @param environment The environment (borrowed).
/// @param result Receives the elg::Theme (borrowed); left as it was on failure.
ELG_EXPORT void elg_env_theme(const EnvironmentValues* environment, Theme* result) {
    ELG_TRY
    *result = environment->get<ThemeKey>();
    ELG_CATCH_VOID
}

/// Reads the environment's foreground (ForegroundKey): the color of text and unfilled shapes. A
/// tracked read.
///
/// @param environment The environment (borrowed).
/// @return The color, or all zeros on failure.
ELG_EXPORT ElgColor elg_env_foreground(const EnvironmentValues* environment) {
    ELG_TRY
    return c_color(environment->get<ForegroundKey>());
    ELG_CATCH(ElgColor{})
}

/// Reads the environment's tint (TintKey): the accent color of controls. A tracked read.
///
/// @param environment The environment (borrowed).
/// @return The color, or all zeros on failure.
ELG_EXPORT ElgColor elg_env_tint(const EnvironmentValues* environment) {
    ELG_TRY
    return c_color(environment->get<TintKey>());
    ELG_CATCH(ElgColor{})
}

/// Reads DisabledKey: controls inside a disabled view don't respond to input and look disabled. A
/// tracked read.
///
/// @param environment The environment (borrowed).
/// @return true if disabled; false on failure.
ELG_EXPORT bool elg_env_disabled(const EnvironmentValues* environment) {
    ELG_TRY
    return environment->get<DisabledKey>();
    ELG_CATCH(false)
}

/// Reads the environment's hover delay (HoverDelayKey). A tracked read.
///
/// @param environment The environment (borrowed).
/// @return The delay, or zeros on failure.
ELG_EXPORT ElgHoverDelay elg_env_hover_delay(const EnvironmentValues* environment) {
    ELG_TRY
    const HoverDelay delay = environment->get<HoverDelayKey>();
    return {delay.enter, delay.exit};
    ELG_CATCH(ElgHoverDelay{})
}

/// Reads the window the views are in (WindowKey). Outside any window, the handle does nothing.
///
/// @param environment The environment (borrowed).
/// @return The window's handle, owned by the caller (elg_window_free()), or nullptr on failure.
ELG_EXPORT WindowHandle* elg_env_window(const EnvironmentValues* environment) {
    ELG_TRY
    return new WindowHandle(environment->get<WindowKey>());
    ELG_CATCH(nullptr)
}

/// Reads the environment's button style (ButtonStyleKey).
///
/// @param environment The environment (borrowed).
/// @return The style, owned by the caller (elg_button_style_free()), or nullptr on failure.
ELG_EXPORT std::shared_ptr<const ButtonStyle>* elg_env_button_style(const EnvironmentValues* environment) {
    ELG_TRY
    return new std::shared_ptr<const ButtonStyle>(environment->get<ButtonStyleKey>());
    ELG_CATCH(nullptr)
}

/// Reads the environment's toggle style (ToggleStyleKey).
///
/// @param environment The environment (borrowed).
/// @return The style, owned by the caller (elg_toggle_style_free()), or nullptr on failure.
ELG_EXPORT std::shared_ptr<const ToggleStyle>* elg_env_toggle_style(const EnvironmentValues* environment) {
    ELG_TRY
    return new std::shared_ptr<const ToggleStyle>(environment->get<ToggleStyleKey>());
    ELG_CATCH(nullptr)
}

/// Reads the environment's text field style (TextFieldStyleKey).
///
/// @param environment The environment (borrowed).
/// @return The style, owned by the caller (elg_text_field_style_free()), or nullptr on failure.
ELG_EXPORT std::shared_ptr<const TextFieldStyle>* elg_env_text_field_style(const EnvironmentValues* environment) {
    ELG_TRY
    return new std::shared_ptr<const TextFieldStyle>(environment->get<TextFieldStyleKey>());
    ELG_CATCH(nullptr)
}

/// Inside a selectable row: whether it's selected (RowSelectedKey). A tracked read.
///
/// @param environment The environment (borrowed).
/// @return true inside a selected row; false elsewhere and on failure.
ELG_EXPORT bool elg_env_row_selected(const EnvironmentValues* environment) {
    ELG_TRY
    return environment->get<RowSelectedKey>();
    ELG_CATCH(false)
}

/// The Rust side's map, or null: the `data` of the closure elg_env_set_rust_values() set.
///
/// @param environment The environment (borrowed).
/// @return The map, owned by the environment; null if none was set, or on failure.
ELG_EXPORT void* elg_env_rust_values(const EnvironmentValues* environment) {
    ELG_TRY
    const SharedFn values = environment->get<RustEnvironmentKey>();
    return values ? values->data() : nullptr;
    ELG_CATCH(nullptr)
}

// Setters, for EnvironmentModifier::apply. Each wraps `EnvironmentValues::set<Key>()`: the value
// applies to this environment and everything derived from it.

/// Sets the font (FontKey).
///
/// @param environment The environment to change (borrowed): an environment modifier's `args`.
/// @param value The font: an ElgProp of elg::Font (kind 0: `ElgFont*`; kinds 1 and 2 write it with
///   elg_font_assign()).
ELG_EXPORT void elg_env_set_font(EnvironmentValues* environment, ElgProp value) {
    ELG_TRY
    environment->set<FontKey>(prop<Font>(value));
    ELG_CATCH_VOID
}
/// Sets the color scheme (ColorSchemeKey).
///
/// @param environment The environment to change (borrowed).
/// @param value The scheme: an ElgProp of elg::ColorScheme (kind 0: `int32_t*`, 0 light, 1 dark).
ELG_EXPORT void elg_env_set_color_scheme(EnvironmentValues* environment, ElgProp value) {
    ELG_TRY
    environment->set<ColorSchemeKey>(prop<ColorScheme>(value));
    ELG_CATCH_VOID
}
/// Sets the theme (ThemeKey).
///
/// @param environment The environment to change (borrowed).
/// @param value The theme: an ElgProp of elg::Theme (kind 0: `elg::Theme*`).
ELG_EXPORT void elg_env_set_theme(EnvironmentValues* environment, ElgProp value) {
    ELG_TRY
    environment->set<ThemeKey>(prop<Theme>(value));
    ELG_CATCH_VOID
}
/// Sets the foreground (ForegroundKey): the color of text and unfilled shapes.
///
/// @param environment The environment to change (borrowed).
/// @param value The color: an ElgProp of elg::Color (kind 0: `ElgColor*`; kind 3: a theme color).
ELG_EXPORT void elg_env_set_foreground(EnvironmentValues* environment, ElgProp value) {
    ELG_TRY
    environment->set<ForegroundKey>(prop<Color>(value));
    ELG_CATCH_VOID
}
/// Sets the tint (TintKey): the accent color of controls.
///
/// @param environment The environment to change (borrowed).
/// @param value The color: an ElgProp of elg::Color (kind 0: `ElgColor*`; kind 3: a theme color).
ELG_EXPORT void elg_env_set_tint(EnvironmentValues* environment, ElgProp value) {
    ELG_TRY
    environment->set<TintKey>(prop<Color>(value));
    ELG_CATCH_VOID
}
/// Sets DisabledKey: controls inside don't respond to input and look disabled.
///
/// @param environment The environment to change (borrowed).
/// @param value Whether they're disabled: an ElgProp of bool (kind 0: `bool*`).
ELG_EXPORT void elg_env_set_disabled(EnvironmentValues* environment, ElgProp value) {
    ELG_TRY
    environment->set<DisabledKey>(prop<bool>(value));
    ELG_CATCH_VOID
}
/// Sets the hover delay (HoverDelayKey), for on_hover handlers without their own and controls.
///
/// @param environment The environment to change (borrowed).
/// @param value The delay: an ElgProp of elg::HoverDelay (kind 0: `ElgHoverDelay*`).
ELG_EXPORT void elg_env_set_hover_delay(EnvironmentValues* environment, ElgProp value) {
    ELG_TRY
    environment->set<HoverDelayKey>(prop<HoverDelay>(value));
    ELG_CATCH_VOID
}
/// Sets RowSelectedKey: whether the views are in a selected row.
///
/// @param environment The environment to change (borrowed).
/// @param value Whether it's selected: an ElgProp of bool (kind 0: `bool*`).
ELG_EXPORT void elg_env_set_row_selected(EnvironmentValues* environment, ElgProp value) {
    ELG_TRY
    environment->set<RowSelectedKey>(prop<bool>(value));
    ELG_CATCH_VOID
}
/// Sets the window the views are in (WindowKey).
///
/// @param environment The environment to change (borrowed).
/// @param window The window's handle (borrowed; copied).
ELG_EXPORT void elg_env_set_window(EnvironmentValues* environment, const WindowHandle* window) {
    ELG_TRY
    environment->set<WindowKey>(*window);
    ELG_CATCH_VOID
}
/// Sets the button style (ButtonStyleKey).
///
/// @param environment The environment to change (borrowed).
/// @param style The style (borrowed; the environment shares it).
ELG_EXPORT void elg_env_set_button_style(EnvironmentValues* environment, const std::shared_ptr<const ButtonStyle>* style) {
    ELG_TRY
    environment->set<ButtonStyleKey>(*style);
    ELG_CATCH_VOID
}
/// Sets the toggle style (ToggleStyleKey).
///
/// @param environment The environment to change (borrowed).
/// @param style The style (borrowed; the environment shares it).
ELG_EXPORT void elg_env_set_toggle_style(EnvironmentValues* environment, const std::shared_ptr<const ToggleStyle>* style) {
    ELG_TRY
    environment->set<ToggleStyleKey>(*style);
    ELG_CATCH_VOID
}
/// Sets the text field style (TextFieldStyleKey).
///
/// @param environment The environment to change (borrowed).
/// @param style The style (borrowed; the environment shares it).
ELG_EXPORT void elg_env_set_text_field_style(EnvironmentValues* environment,
                                             const std::shared_ptr<const TextFieldStyle>* style) {
    ELG_TRY
    environment->set<TextFieldStyleKey>(*style);
    ELG_CATCH_VOID
}
/// Sets the Rust side's map of environment values, which elg_env_rust_values() returns.
///
/// @param environment The environment to change (borrowed).
/// @param values Consumed closure that's never called: its `data` is the map, and its `drop` runs
///   once no environment refers to it.
ELG_EXPORT void elg_env_set_rust_values(EnvironmentValues* environment, ElgClosure values) {
    ELG_TRY
    environment->set<RustEnvironmentKey>(SharedFn(std::make_shared<const RustFn>(values)));
    ELG_CATCH_VOID
}

// --- Windows ---

/// Copies a window handle; the copy refers to the same window.
///
/// @param window The handle (borrowed).
/// @return The copy, owned by the caller.
ELG_EXPORT WindowHandle* elg_window_clone(const WindowHandle* window) {
    return new WindowHandle(*window);
}
/// Frees a window handle (not the window).
///
/// @param window The handle (consumed); may be null.
ELG_EXPORT void elg_window_free(WindowHandle* window) {
    delete window;
}
/// Whether two handles refer to the same window (`WindowHandle`'s operator==).
///
/// @param a A handle (borrowed).
/// @param b Another handle (borrowed).
/// @return true for the same window, or for two handles outside any window.
ELG_EXPORT bool elg_window_eq(const WindowHandle* a, const WindowHandle* b) {
    return *a == *b;
}
/// Wraps `WindowHandle::close()`: like the system close button, the window closes (the app quits
/// with its last window).
///
/// @param window The window's handle (borrowed).
ELG_EXPORT void elg_window_close(const WindowHandle* window) {
    ELG_TRY
    window->close();
    ELG_CATCH_VOID
}
/// Wraps `WindowHandle::minimize()`.
///
/// @param window The window's handle (borrowed).
ELG_EXPORT void elg_window_minimize(const WindowHandle* window) {
    ELG_TRY
    window->minimize();
    ELG_CATCH_VOID
}
/// Wraps `WindowHandle::toggle_maximized()`: maximizes the window, or restores a maximized one.
///
/// @param window The window's handle (borrowed).
ELG_EXPORT void elg_window_toggle_maximized(const WindowHandle* window) {
    ELG_TRY
    window->toggle_maximized();
    ELG_CATCH_VOID
}
/// Wraps `WindowHandle::is_maximized()`. A tracked read.
///
/// @param window The window's handle (borrowed).
/// @return true if the window is maximized; false outside any window and on failure.
ELG_EXPORT bool elg_window_is_maximized(const WindowHandle* window) {
    ELG_TRY
    return window->is_maximized();
    ELG_CATCH(false)
}
/// Wraps `WindowHandle::is_active()`: whether the window has keyboard focus (title bars usually dim
/// when it doesn't). A tracked read.
///
/// @param window The window's handle (borrowed).
/// @return true if the window is active, and outside any window; false on failure.
ELG_EXPORT bool elg_window_is_active(const WindowHandle* window) {
    ELG_TRY
    return window->is_active();
    ELG_CATCH(false)
}
/// Wraps `WindowHandle::title_bar()`: the window's WindowDesc::title_bar.
///
/// @param window The window's handle (borrowed).
/// @return The elg::TitleBar: 0 system, 1 hidden, 2 integrated. 0 outside any window and on
///   failure.
ELG_EXPORT int32_t elg_window_title_bar(const WindowHandle* window) {
    ELG_TRY
    return static_cast<int32_t>(window->title_bar());
    ELG_CATCH(0)
}
/// Wraps `WindowHandle::system_controls_frame()`: where the system draws its window controls over
/// the content (the traffic lights of a TitleBar::integrated window on macOS), in the window's
/// points. Keep content clear of it.
///
/// @param window The window's handle (borrowed).
/// @return The frame; empty where the system doesn't draw over the content, and on failure.
ELG_EXPORT ElgRect elg_window_system_controls_frame(const WindowHandle* window) {
    ELG_TRY
    return c_rect(window->system_controls_frame());
    ELG_CATCH(ElgRect{})
}

/// Wraps `WindowControls()`: minimize, maximize/restore, and close buttons for a custom title bar.
/// They act on the window they're in, and dim while it isn't active.
///
/// @param style An elg::WindowControlsStyle: 0 automatic (the platform's, and only in
///   TitleBar::hidden windows), 1 caption buttons, 2 traffic lights.
/// @param minimize Whether to show the minimize button.
/// @param maximize Whether to show the maximize/restore button.
/// @return The new view, owned by the caller, or nullptr on failure.
ELG_EXPORT View* elg_window_controls(int32_t style, bool minimize, bool maximize) {
    ELG_TRY
    return out(WindowControls({static_cast<WindowControlsStyle>(style), minimize, maximize}));
    ELG_CATCH(nullptr)
}

// --- App ---

/// An elg::WindowDesc: how a window opens (elg_app_window()). Sizes are in logical points.
struct ElgWindowDesc {
    ElgStr title;                      ///< The window's title.
    int32_t width;                     ///< Initial width.
    int32_t height;                    ///< Initial height.
    bool resizable;                    ///< Whether the user can resize it.
    bool has_min_size;                 ///< false: no minimum size.
    ElgSize min_size;                  ///< Smallest size the user can resize to, if `has_min_size`.
    /// false: the default background, the theme's (ThemeColor::background), or clear for
    /// transparent windows.
    bool has_background;
    ElgColor background;               ///< Shown where no view draws, if `has_background`.
    int32_t title_bar;                 ///< An elg::TitleBar: 0 system, 1 hidden, 2 integrated.
    bool has_system_controls_position; ///< false: the system's window controls go where it puts them.
    /// TitleBar::integrated on macOS: where the system's window controls go, as the first one's
    /// top-left corner in points from the window's top-left corner.
    ElgPoint system_controls_position;
    bool transparent;                  ///< The desktop shows through where the content isn't opaque.
    float opacity;                     ///< The whole window's opacity, 0 to 1.
    /// Covers the whole display it opens on, without a frame. `width` and `height` are its size
    /// when it leaves fullscreen.
    bool fullscreen;
};

/// Wraps `App()`: owns the application's lifetime, windows, event loop, and render thread.
///
/// @return The new app, owned by the caller (elg_app_free()), or nullptr on failure.
ELG_EXPORT App* elg_app_new() {
    ELG_TRY
    return new App();
    ELG_CATCH(nullptr)
}

/// Destroys an app.
///
/// @param app The app (consumed); may be null.
ELG_EXPORT void elg_app_free(App* app) {
    ELG_TRY
    delete app;
    ELG_CATCH_VOID
}

/// Wraps `App::window()`: adds a window that opens when the app runs. `content` (ret: View*)
/// builds the window's root view when it opens. Records a UsageError while the app runs.
///
/// @param app The app (borrowed).
/// @param desc How the window opens (borrowed).
/// @param content Consumed closure, called once on the UI thread; args: null, ret: `View*` (set
///   it with elg_view_move_into()).
ELG_EXPORT void elg_app_window(App* app, const ElgWindowDesc* desc, ElgClosure content) {
    ELG_TRY
    WindowDesc window;
    window.title = str(desc->title);
    window.width = desc->width;
    window.height = desc->height;
    window.resizable = desc->resizable;
    if (desc->has_min_size) {
        window.min_size = size(desc->min_size);
    }
    if (desc->has_background) {
        window.background = color(desc->background);
    }
    window.title_bar = static_cast<TitleBar>(desc->title_bar);
    if (desc->has_system_controls_position) {
        window.system_controls_position = point(desc->system_controls_position);
    }
    window.transparent = desc->transparent;
    window.opacity = desc->opacity;
    window.fullscreen = desc->fullscreen;
    const SharedFn f = std::make_shared<const RustFn>(content);
    app->window(std::move(window), [f]() -> View {
        View result;
        f->call(nullptr, &result);
        return result;
    });
    ELG_CATCH_VOID
}

/// Wraps `App::run()`: opens the windows and runs the event loop until the last window closes or
/// elg_app_quit() is called. Errors and panics in app callbacks end it after a clean shutdown.
///
/// @param app The app (borrowed).
/// @return The process exit code, or -1 on failure (no windows, already running, or an error that
///   propagated out of a callback).
ELG_EXPORT int32_t elg_app_run(App* app) {
    ELG_TRY
    return app->run();
    ELG_CATCH(-1)
}

/// Wraps `App::quit()`: makes elg_app_run() return after the current event. Call it on the UI
/// thread.
///
/// @param app The app (borrowed).
ELG_EXPORT void elg_app_quit(App* app) {
    ELG_TRY
    app->quit();
    ELG_CATCH_VOID
}

/// Wraps `post_to_ui_thread()`: runs `task` on the UI thread soon, between events, where state may
/// be written. Callable from any thread; tasks run in the order posted. Posted while no App runs,
/// a task waits for the next one.
///
/// @param task Consumed closure, a Send closure; it runs on the UI thread. args and ret: null.
ELG_EXPORT void elg_post_to_ui_thread(ElgClosure task) {
    ELG_TRY
    post_to_ui_thread(action(task));
    ELG_CATCH_VOID
}

/// Wraps `version()`: the library's version string, e.g. "0.1.0".
///
/// @return A static, NUL-terminated string; don't free it.
ELG_EXPORT const char* elg_version() {
    return version();
}

// --- Animation ---

/// Wraps `Animation::linear()`: constant speed.
///
/// @param duration Seconds (the C++ default is 0.25); negative counts as 0.
/// @return The animation, owned by the caller (elg_animation_free()).
ELG_EXPORT Animation* elg_animation_linear(double duration) {
    return new Animation(Animation::linear(duration));
}
/// Wraps `Animation::ease_in()`: starts slowly.
///
/// @param duration Seconds (the C++ default is 0.25); negative counts as 0.
/// @return The animation, owned by the caller (elg_animation_free()).
ELG_EXPORT Animation* elg_animation_ease_in(double duration) {
    return new Animation(Animation::ease_in(duration));
}
/// Wraps `Animation::ease_out()`: ends slowly.
///
/// @param duration Seconds (the C++ default is 0.25); negative counts as 0.
/// @return The animation, owned by the caller (elg_animation_free()).
ELG_EXPORT Animation* elg_animation_ease_out(double duration) {
    return new Animation(Animation::ease_out(duration));
}
/// Wraps `Animation::ease_in_out()`: starts and ends slowly.
///
/// @param duration Seconds (the C++ default is 0.35); negative counts as 0.
/// @return The animation, owned by the caller (elg_animation_free()).
ELG_EXPORT Animation* elg_animation_ease_in_out(double duration) {
    return new Animation(Animation::ease_in_out(duration));
}
/// Wraps `Animation::spring()`: a spring settling at the target, like SwiftUI's
/// .spring(response:dampingFraction:).
///
/// @param response Roughly the time to get there, in seconds (the C++ default is 0.5).
/// @param damping_fraction 1 settles without overshoot; lower values bounce (the C++ default is
///   0.825).
/// @return The animation, owned by the caller (elg_animation_free()).
ELG_EXPORT Animation* elg_animation_spring(double response, double damping_fraction) {
    return new Animation(Animation::spring(response, damping_fraction));
}
/// Wraps `Animation::smooth()`: a gentle spring without overshoot.
///
/// @param duration Roughly the time to get there, in seconds (the C++ default is 0.4).
/// @return The animation, owned by the caller (elg_animation_free()).
ELG_EXPORT Animation* elg_animation_smooth(double duration) {
    return new Animation(Animation::smooth(duration));
}
/// Wraps `Animation::custom()`: `curve` (args: double* t, ret: double*) maps progress 0...1 to
/// eased progress. It may go past 0 or 1 to overshoot; the animation lands on the target once
/// `duration` is up regardless.
///
/// @param curve Consumed closure; args: `double*`, the fraction of `duration` elapsed (0 to 1);
///   ret: `double*`, the progress (0 at the start, 1 at the target), set to the fraction beforehand.
///   A panic in it propagates like one in any callback: from an animated property, out of
///   App::run; from elg_animation_progress(), as its error.
/// @param duration Seconds; negative counts as 0.
/// @return The animation, owned by the caller (elg_animation_free()), or nullptr on failure.
ELG_EXPORT Animation* elg_animation_custom(ElgClosure curve, double duration) {
    ELG_TRY
    const SharedFn f = std::make_shared<const RustFn>(curve);
    return new Animation(Animation::custom(
        [f](double t) {
            double result = t;
            f->call(&t, &result);
            return result;
        },
        duration));
    ELG_CATCH(nullptr)
}
/// Wraps `Animation::delay()`: a copy of `animation` that waits before starting.
///
/// @param animation The animation (borrowed).
/// @param seconds How long to wait, added to any delay it has. Negative starts partway in, like
///   CSS: -0.1 begins where it would be 0.1 s in.
/// @return The new animation, owned by the caller (elg_animation_free()).
ELG_EXPORT Animation* elg_animation_delay(const Animation* animation, double seconds) {
    return new Animation(animation->delay(seconds));
}
/// Wraps `Animation::progress()`.
///
/// @param animation The animation (borrowed).
/// @param seconds Time since the start.
/// @return The progress: 0 at the start, 1 at the end (springs overshoot). 1 on failure.
ELG_EXPORT double elg_animation_progress(const Animation* animation, double seconds) {
    ELG_TRY
    return animation->progress(seconds);
    ELG_CATCH(1.0)
}
/// Wraps `Animation::finished()`.
///
/// @param animation The animation (borrowed).
/// @param seconds Time since the start.
/// @return true once it has finished (never for one that repeats forever); true on failure.
ELG_EXPORT bool elg_animation_finished(const Animation* animation, double seconds) {
    ELG_TRY
    return animation->finished(seconds);
    ELG_CATCH(true)
}
/// Copies an animation.
///
/// @param animation The animation (borrowed).
/// @return The copy, owned by the caller (elg_animation_free()).
ELG_EXPORT Animation* elg_animation_clone(const Animation* animation) {
    return new Animation(*animation);
}
/// Frees an animation.
///
/// @param animation The animation (consumed); may be null.
ELG_EXPORT void elg_animation_free(Animation* animation) {
    delete animation;
}

// --- Transition ---

/// A Transition::Pose without its anchor: how a view looks partway through a transition.
struct ElgPose {
    float opacity, scale, x, y; ///< Opacity and scale (1: as is), and the offset in points.
};

/// Wraps `Transition::opacity()`: the view fades in and out.
///
/// @return The transition, owned by the caller (elg_transition_free()).
ELG_EXPORT Transition* elg_transition_opacity() {
    return new Transition(Transition::opacity());
}
/// Wraps `Transition::identity()`: the view appears and disappears at once.
///
/// @return The transition, owned by the caller (elg_transition_free()).
ELG_EXPORT Transition* elg_transition_identity() {
    return new Transition(Transition::identity());
}
/// Wraps `Transition::scale()`: the view grows from (and shrinks to) `from` times its size.
///
/// @param from The scale it starts and ends at (the C++ default is 0).
/// @param anchor The point of its frame it scales around.
/// @return The transition, owned by the caller (elg_transition_free()).
ELG_EXPORT Transition* elg_transition_scale(float from, ElgAlignment anchor) {
    return new Transition(Transition::scale(from, alignment(anchor)));
}
/// Wraps `Transition::offset()`: the view moves in from (and out to) an offset.
///
/// @param x Horizontal offset in points.
/// @param y Vertical offset in points.
/// @return The transition, owned by the caller (elg_transition_free()).
ELG_EXPORT Transition* elg_transition_offset(float x, float y) {
    return new Transition(Transition::offset(x, y));
}
/// Wraps `Transition::move()`: the view slides in from (and out to) beyond an edge of its frame.
///
/// @param edge elg::Edges bits: 1 top, 2 leading, 4 bottom, 8 trailing.
/// @return The transition, owned by the caller (elg_transition_free()).
ELG_EXPORT Transition* elg_transition_move(uint32_t edge) {
    return new Transition(Transition::move(static_cast<Edges>(edge)));
}
/// Wraps `Transition::slide()`: the view moves in from the leading edge and out to the trailing
/// edge.
///
/// @return The transition, owned by the caller (elg_transition_free()).
ELG_EXPORT Transition* elg_transition_slide() {
    return new Transition(Transition::slide());
}
/// Wraps `Transition::asymmetric()`: different transitions for insertion and removal.
///
/// @param insertion How the view enters (borrowed).
/// @param removal How the view leaves (borrowed).
/// @return The transition, owned by the caller (elg_transition_free()).
ELG_EXPORT Transition* elg_transition_asymmetric(const Transition* insertion, const Transition* removal) {
    return new Transition(Transition::asymmetric(*insertion, *removal));
}
/// Wraps `Transition::combined()`: both transitions at once.
///
/// @param a A transition (borrowed).
/// @param b Another transition (borrowed).
/// @return The transition, owned by the caller (elg_transition_free()).
ELG_EXPORT Transition* elg_transition_combined(const Transition* a, const Transition* b) {
    return new Transition(a->combined(*b));
}
/// Wraps `Transition::pose()`: how the view looks at `progress`.
///
/// @param transition The transition (borrowed).
/// @param progress 0: fully out, 1: in place.
/// @param removal true for the view leaving, false for it entering.
/// @param view_size The view's size in points, for transitions that move it beyond an edge.
/// @return The pose.
ELG_EXPORT ElgPose elg_transition_pose(const Transition* transition, float progress, bool removal, ElgSize view_size) {
    const Transition::Pose pose = transition->pose(progress, removal, size(view_size));
    return {pose.opacity, pose.scale, pose.x, pose.y};
}
/// Copies a transition.
///
/// @param transition The transition (borrowed).
/// @return The copy, owned by the caller (elg_transition_free()).
ELG_EXPORT Transition* elg_transition_clone(const Transition* transition) {
    return new Transition(*transition);
}
/// Frees a transition.
///
/// @param transition The transition (consumed); may be null.
ELG_EXPORT void elg_transition_free(Transition* transition) {
    delete transition;
}

// --- ABI layout (shim.h's structs and this file's) ---

/// Describes the layout of shim.h's structs and this file's, for elegance-sys's layout test: one
/// line per struct, "name size align".
///
/// @param out Receives each line.
/// @param ctx Passed to `out`.
ELG_EXPORT void elg_layouts_core(ElgStrSink out, void* ctx) {
    ELG_LAYOUT(ElgStr);
    ELG_LAYOUT(ElgBytes);
    ELG_LAYOUT(ElgClosure);
    ELG_LAYOUT(ElgColor);
    ELG_LAYOUT(ElgSize);
    ELG_LAYOUT(ElgPixelSize);
    ELG_LAYOUT(ElgPoint);
    ELG_LAYOUT(ElgRect);
    ELG_LAYOUT(ElgFont);
    ELG_LAYOUT(ElgGradientStop);
    ELG_LAYOUT(ElgShapeStyle);
    ELG_LAYOUT(ElgEdgeInsets);
    ELG_LAYOUT(ElgProp);
    ELG_LAYOUT(ElgProposedSize);
    ELG_LAYOUT(ElgOptLen);
    ELG_LAYOUT(ElgAlignment);
    ELG_LAYOUT(ElgKeyEvent);
    ELG_LAYOUT(ElgPointerEvent);
    ELG_LAYOUT(ElgScrollEvent);
    ELG_LAYOUT(ElgClickEvent);
    ELG_LAYOUT(ElgDragEvent);
    ELG_LAYOUT(ElgMagnifyEvent);
    ELG_LAYOUT(ElgRotateEvent);
    ELG_LAYOUT(ElgTextInputEvent);
    ELG_LAYOUT(ElgEventResult);
    ELG_LAYOUT(ElgFloatingPlacement);
    ELG_LAYOUT(ElgPopupMotion);
    ELG_LAYOUT(ElgHoverDelay);
    ELG_LAYOUT(ElgShadow);
    ELG_LAYOUT(ElgStrokeStyle);
    ELG_LAYOUT(ElgRunStyle);
    ELG_LAYOUT(ElgImageSource);
    ELG_LAYOUT(ElgWindowDesc);
    ELG_LAYOUT(ElgPose);
}
