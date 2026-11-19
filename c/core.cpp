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

int32_t elg_shim::error_kind(const std::exception_ptr& error, std::string& message) noexcept {
    try {
        try {
            std::rethrow_exception(error);
        } catch (const RustPanic&) {
            message = "a Rust callback panicked";
            return panic_error;
        } catch (const UsageError& e) {
            message = e.what();
            return usage_error;
        } catch (const ResourceError& e) {
            message = e.what();
            return resource_error;
        } catch (const GpuError& e) {
            message = e.what();
            return gpu_error;
        } catch (const PlatformError& e) {
            message = e.what();
            return platform_error;
        } catch (const Error& e) {
            message = e.what();
            return elegance_error;
        } catch (const std::exception& e) {
            message = e.what();
            return other_error;
        } catch (...) {
            message = "unknown C++ exception";
            return other_error;
        }
    } catch (...) {
        return other_error; // the message couldn't be copied: out of memory
    }
}

void elg_shim::capture_error() noexcept {
    g_error_kind = error_kind(std::current_exception(), g_error_message);
}

void elg_shim::require_ui_thread(const char* function) {
    try {
        elg::detail::check_ui_thread();
    } catch (const UsageError&) {
        throw UsageError(std::format("{}() was called off the UI thread of the running App; only the functions "
                                     "documented as callable from any thread may be",
                                     function));
    }
}

bool elg_shim::off_ui_thread() noexcept {
    try {
        elg::detail::check_ui_thread();
        return false;
    } catch (...) {
        return true;
    }
}

void elg_shim::release_on_ui_thread(Leftovers&& leftovers) noexcept {
    if (leftovers.empty()) {
        return;
    }
    Leftovers* held = nullptr;
    try {
        held = new Leftovers(std::move(leftovers));
    } catch (...) {
        leftovers.release(); // out of memory: here, rather than not at all
        return;
    }
    elg::detail::release_on_ui_thread(held, [](void* object) noexcept {
        auto* const handed_over = static_cast<Leftovers*>(object);
        handed_over->release();
        delete handed_over;
    });
}

/// Takes this thread's pending error: the pending error's kind (0: none), with its message sent to
/// `out`; clears it. A call that fails records its exception here, replacing any earlier one.
/// Callable from any thread.
///
/// @param out Receives the message, if there's an error.
/// @param ctx Passed to `out`.
/// @return The error's kind: 0 none, 1 a Rust callback panicked, 2 elg::UsageError,
///   3 elg::ResourceError, 4 elg::GpuError, 5 elg::PlatformError, 6 another elg::Error, 7 any other
///   C++ exception. 0 on failure.
ELG_EXPORT int32_t elg_take_error(ElgStrSink out, void* ctx) {
    ELG_TRY_ANY_THREAD
    const int32_t kind = std::exchange(g_error_kind, no_error);
    if (kind != no_error) {
        sink(out, ctx, g_error_message);
        g_error_message.clear();
    }
    return kind;
    ELG_CATCH(0)
}

/// Whether this thread has an error that elg_take_error() hasn't taken yet.
/// Callable from any thread.
///
/// @return true if an error is pending. False on failure.
ELG_EXPORT bool elg_error_pending() {
    ELG_TRY_ANY_THREAD
    return g_error_kind != no_error;
    ELG_CATCH(false)
}

/// Sets a C++ std::string: how a closure returns a string (ret: `std::string*`), as a
/// `Binding<std::string>` getter or a string prop does.
///
/// @param target The string to set (borrowed).
/// @param value The new contents, copied.
ELG_EXPORT void elg_string_assign(std::string* target, ElgStr value) {
    ELG_TRY
    target->assign(view(value));
    ELG_CATCH_VOID
}

/// The contents of a C++ std::string, such as a closure's `std::string*` argument.
///
/// @param s The string (borrowed).
/// @return Its bytes, valid while `s` lives and doesn't change. Empty on failure.
ELG_EXPORT ElgStr elg_string_view(const std::string* s) {
    ELG_TRY
    return c_str(*s);
    ELG_CATCH({})
}

// --- Views ---

/// Wraps `View()`: an empty view, which takes no space and draws nothing.
///
/// @return The new view, owned by the caller. Null on failure.
ELG_EXPORT View* elg_view_empty() {
    ELG_TRY
    return new View();
    ELG_CATCH(nullptr)
}

/// Copies a view (an immutable value, cheap to copy).
///
/// @param view The view (borrowed).
/// @return The copy, owned by the caller. Null on failure.
ELG_EXPORT View* elg_view_clone(const View* view) {
    ELG_TRY
    return new View(*view);
    ELG_CATCH(nullptr)
}

/// Frees a view.
///
/// @param view The view (consumed); may be null.
ELG_EXPORT void elg_view_free(View* view) {
    const Consumed consumed(view); // freed as the call ends
    ELG_TRY
    ELG_CATCH_VOID
}

/// Moves `take_view` into `*target` (a closure's View result).
///
/// @param target The closure's `ret` (borrowed); what it held is replaced.
/// @param take_view The view (consumed).
ELG_EXPORT void elg_view_move_into(View* target, View* take_view) {
    const Consumed consumed(take_view);
    ELG_TRY
    *target = take(take_view);
    ELG_CATCH_VOID
}

/// Wraps `View::is_empty()`: whether this is an empty view (View(), EmptyView()), e.g. a style's
/// label for a control made without one.
///
/// @param view The view (borrowed).
/// @return true if it's empty. False on failure.
ELG_EXPORT bool elg_view_is_empty(const View* view) {
    ELG_TRY
    return view->is_empty();
    ELG_CATCH(false)
}

/// A Rust component: `body` (ret: View*) runs each time the view is mounted. The C++ side of a
/// component (a type with `View body() const`).
///
/// @param body Consumed closure; args: null, ret: `View*` (set it with elg_view_move_into()).
///   Required: none is a usage error.
/// @return The new view, owned by the caller, or nullptr on failure.
ELG_EXPORT View* elg_view_component(ElgClosure body) {
    const Consumed consumed(body);
    ELG_TRY
    return out(View(RustComponent{required(body, "body")}));
    ELG_CATCH(nullptr)
}

// --- Reactive state ---
//
// Rust keeps the values; each State/Computed/Binding has a C++ node for dependency tracking and
// lifetime. Nodes belong to the current Scope (the mounted view tree, If branch, ForEach item...),
// so they end with the views that made them. `owner`'s drop frees the Rust side's slot.

namespace {

// The handles the C API gives out for reactive nodes: a slot's index (plus 1, so that 0 is no
// handle) in the low 32 bits and the slot's generation in the high 32. When a node ends, its slot
// gets the next generation, so a handle to it fails to find it, also once the slot (or the node's
// memory) has been reused.
class NodeHandles {
public:
    std::uint64_t add(detail::ReactiveNode* node) {
        std::uint32_t index = 0;
        if (free_.empty()) {
            index = static_cast<std::uint32_t>(slots_.size());
            slots_.push_back({});
        } else {
            index = free_.back();
            free_.pop_back();
        }
        slots_[index].node = node;
        return (static_cast<std::uint64_t>(slots_[index].generation) << 32) | (index + 1);
    }

    void remove(std::uint32_t index) noexcept {
        Slot& slot = slots_[index];
        slot.node = nullptr;
        slot.generation = slot.generation == UINT32_MAX ? 1 : slot.generation + 1;
        try {
            free_.push_back(index);
        } catch (...) {
            // Swallowed: out of memory, so the slot isn't used again.
        }
    }

    // The node `handle` refers to, or a UsageError once it has ended.
    [[nodiscard]] detail::ReactiveNode& node(std::uint64_t handle) const {
        const std::uint64_t index = (handle & UINT32_MAX) - 1;
        if (handle == 0 || index >= slots_.size() || slots_[index].generation != handle >> 32 || !slots_[index].node) {
            throw UsageError("a handle to reactive state was used after the state ended (with the views or "
                             "scope that made it)");
        }
        return *slots_[index].node;
    }

private:
    struct Slot {
        detail::ReactiveNode* node = nullptr;
        std::uint32_t generation = 1;
    };
    std::vector<Slot> slots_;
    std::vector<std::uint32_t> free_;
};

NodeHandles& handles() {
    // Leaked, like roots(): nodes made outside any scope never end.
    static auto* const instance = new NodeHandles();
    return *instance;
}

// Part of a node made through the C API: removes the node's handle when the node ends.
struct HandleSlot {
    std::uint32_t index = UINT32_MAX; // none yet
    HandleSlot() = default;
    HandleSlot(const HandleSlot&) = delete;
    HandleSlot& operator=(const HandleSlot&) = delete;
    HandleSlot(HandleSlot&&) = delete;
    HandleSlot& operator=(HandleSlot&&) = delete;
    ~HandleSlot() {
        if (index != UINT32_MAX) {
            handles().remove(index);
        }
    }
};

// A signal node's value: the Rust side's slot (`owner`, dropped when the node ends), and the
// node's handle.
struct SignalOwner {
    SharedFn owner;
    std::shared_ptr<HandleSlot> slot = std::make_shared<HandleSlot>();
};

using OwnerNode = detail::SignalNode<SignalOwner>;

// Registers `node`, whose `slot` removes it when it ends, and returns its handle.
std::uint64_t handle_for(detail::ReactiveNode* node, HandleSlot& slot) {
    const std::uint64_t handle = handles().add(node);
    slot.index = static_cast<std::uint32_t>((handle & UINT32_MAX) - 1);
    return handle;
}

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
/// @return A handle to the node, for elg_node_track() and elg_node_notify(); its scope owns the
///   node, not the caller. Once the node has ended, using the handle is a usage error. 0 on failure.
ELG_EXPORT uint64_t elg_signal_new(ElgClosure owner) {
    const Consumed consumed(owner);
    ELG_TRY
    auto node = std::make_shared<OwnerNode>(SignalOwner{.owner = hold(owner)});
    const std::uint64_t handle = handle_for(node.get(), *node->value.slot);
    adopt(std::move(node));
    return handle;
    ELG_CATCH(0)
}

/// A computed node: the C++ side of a Rust Computed, owned by the current Scope like
/// elg_signal_new()'s. `compute` (ret: uint64_t*) brings the Rust value up to date and returns a
/// version that changes whenever the value does.
///
/// @param compute Consumed closure, run when the node is read and may have changed; args: null,
///   ret: `uint64_t*`. When the version stays the same, the node's dependents don't rerun.
///   Required: none is a usage error.
/// @return A handle to the node, for elg_node_track(); its scope owns the node, not the caller.
///   Once the node has ended, using the handle is a usage error. 0 on failure.
ELG_EXPORT uint64_t elg_computed_new(ElgClosure compute) {
    const Consumed consumed(compute);
    ELG_TRY
    const SharedFn f = required(compute, "compute");
    auto slot = std::make_shared<HandleSlot>(); // goes with the node's function, so with the node
    auto node = std::make_shared<detail::ComputedNode<std::uint64_t>>([f, slot] {
        std::uint64_t version = 0;
        f->call(nullptr, &version);
        return version;
    });
    const std::uint64_t handle = handle_for(node.get(), *slot);
    adopt(std::move(node));
    return handle;
    ELG_CATCH(0)
}

/// A read (`ReactiveNode::track()`): subscribes the running computed/effect, bringing a computed
/// up to date first.
///
/// @param node A handle from elg_signal_new() or elg_computed_new(); one whose node has ended is a
///   usage error.
ELG_EXPORT void elg_node_track(uint64_t node) {
    ELG_TRY
    handles().node(node).track();
    ELG_CATCH_VOID
}

/// A write happened (UI thread only): `ReactiveNode::notify_changed()` marks the node's dependents
/// and schedules the effects it affects. Off the UI thread, records a UsageError instead.
///
/// @param node A handle from elg_signal_new(); one whose node has ended is a usage error.
ELG_EXPORT void elg_node_notify(uint64_t node) {
    ELG_TRY
    handles().node(node).notify_changed();
    ELG_CATCH_VOID
}

/// Records a UsageError when called off the UI thread of a running App, as every function of the
/// UI thread does: for a binding to check where it is before work of its own.
ELG_EXPORT void elg_check_ui_thread() {
    ELG_TRY
    ELG_CATCH_VOID
}

/// Wraps `elg::effect()`: runs `function` now and again whenever state it read changes (after the
/// current event, before the next frame). Call it while building views; it ends when those views
/// are removed. Elsewhere it records a UsageError.
///
/// @param function Consumed closure; args and ret: null. Required: none is a usage error.
ELG_EXPORT void elg_effect(ElgClosure function) {
    const Consumed consumed(function);
    ELG_TRY
    effect(action(required(function, "function")));
    ELG_CATCH_VOID
}

/// Runs `function` (borrowed) without tracking reads: `elg::untrack()`.
///
/// @param function Borrowed closure, called once before this returns; args and ret: null.
///   Required: none is a usage error.
ELG_EXPORT void elg_untrack(ElgClosure function) {
    const Consumed consumed(function);
    ELG_TRY
    const SharedFn f = required(function, "function");
    untrack([&] { f->call(nullptr, nullptr); });
    ELG_CATCH_VOID
}

/// Wraps `elg::batch()`: runs `function`, grouping its writes so that effects run once, after the
/// outermost batch ends.
///
/// @param function Borrowed closure, called once before this returns; args and ret: null.
///   Required: none is a usage error.
ELG_EXPORT void elg_batch(ElgClosure function) {
    const Consumed consumed(function);
    ELG_TRY
    const SharedFn f = required(function, "function");
    batch([&] { f->call(nullptr, nullptr); });
    ELG_CATCH_VOID
}

/// Wraps `elg::with_animation()`: runs `changes` in an animated transaction, so every property
/// change its writes cause animates with `animation`, including layout and If/ForEach insertions
/// and removals.
///
/// @param animation The animation (borrowed).
/// @param changes Borrowed closure, called once before this returns; args and ret: null.
///   Required: none is a usage error.
ELG_EXPORT void elg_with_animation(const Animation* animation, ElgClosure changes) {
    const Consumed consumed(changes);
    ELG_TRY
    const SharedFn f = required(changes, "changes");
    with_animation(*animation, [&] { f->call(nullptr, nullptr); });
    ELG_CATCH_VOID
}

// --- Bindings to the C++ value types controls take ---

// Takes over `get` and `set`; the caller's Consumed releases what this doesn't.
template <class T>
static Binding<T>* make_binding(ElgClosure& get, ElgClosure& set) {
    const SharedFn g = required(get, "get");
    const SharedFn s = required(set, "set");
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
/// @param get Consumed closure; args: null, ret: `bool*`. Required: none is a usage error.
/// @param set Consumed closure; args: `bool*` (the new value), ret: null. Required: none is a
///   usage error.
/// @return The binding, owned by the caller until a function takes it (`take_*`) or
///   elg_binding_bool_free() frees it; nullptr on failure.
ELG_EXPORT Binding<bool>* elg_binding_bool(ElgClosure get, ElgClosure set) {
    const Consumed consumed(get, set);
    ELG_TRY
    return make_binding<bool>(get, set);
    ELG_CATCH(nullptr)
}
/// A `Binding<float>` over a Rust value, for a control: `get` reads it, `set` writes it.
///
/// @param get Consumed closure; args: null, ret: `float*`. Required: none is a usage error.
/// @param set Consumed closure; args: `float*` (the new value), ret: null. Required: none is a
///   usage error.
/// @return The binding, owned by the caller until a function takes it (`take_*`) or
///   elg_binding_f32_free() frees it; nullptr on failure.
ELG_EXPORT Binding<float>* elg_binding_f32(ElgClosure get, ElgClosure set) {
    const Consumed consumed(get, set);
    ELG_TRY
    return make_binding<float>(get, set);
    ELG_CATCH(nullptr)
}
/// A `Binding<int>` over a Rust value, for a control: `get` reads it, `set` writes it.
///
/// @param get Consumed closure; args: null, ret: `int32_t*`. Required: none is a usage error.
/// @param set Consumed closure; args: `int32_t*` (the new value), ret: null. Required: none is a
///   usage error.
/// @return The binding, owned by the caller until a function takes it (`take_*`) or
///   elg_binding_i32_free() frees it; nullptr on failure.
ELG_EXPORT Binding<int>* elg_binding_i32(ElgClosure get, ElgClosure set) {
    const Consumed consumed(get, set);
    ELG_TRY
    return make_binding<int>(get, set);
    ELG_CATCH(nullptr)
}
/// A `Binding<std::string>` over a Rust value, for a control: `get` reads it, `set` writes it.
///
/// @param get Consumed closure; args: null, ret: `std::string*` (set it with elg_string_assign()).
///   Required: none is a usage error.
/// @param set Consumed closure; args: `std::string*`, the new value (read it with elg_string_view());
///   ret: null. Required: none is a usage error.
/// @return The binding, owned by the caller until a function takes it (`take_*`) or
///   elg_binding_string_free() frees it; nullptr on failure.
ELG_EXPORT Binding<std::string>* elg_binding_string(ElgClosure get, ElgClosure set) {
    const Consumed consumed(get, set);
    ELG_TRY
    return make_binding<std::string>(get, set);
    ELG_CATCH(nullptr)
}
/// A `Binding<double>` over a Rust value, for a control: `get` reads it, `set` writes it.
///
/// @param get Consumed closure; args: null, ret: `double*`. Required: none is a usage error.
/// @param set Consumed closure; args: `double*` (the new value), ret: null. Required: none is a
///   usage error.
/// @return The binding, owned by the caller until a function takes it (`take_*`) or
///   elg_binding_f64_free() frees it; nullptr on failure.
ELG_EXPORT Binding<double>* elg_binding_f64(ElgClosure get, ElgClosure set) {
    const Consumed consumed(get, set);
    ELG_TRY
    return make_binding<double>(get, set);
    ELG_CATCH(nullptr)
}

/// Writes an optional float into a closure's std::optional<float> result.
///
/// @param target The closure's `ret` (borrowed).
/// @param value The value; `has` false: nullopt.
ELG_EXPORT void elg_opt_float_assign(std::optional<float>* target, ElgOptLen value) {
    ELG_TRY
    *target = value.has ? std::optional<float>(value.value) : std::nullopt;
    ELG_CATCH_VOID
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
    const Consumed consumed(binding); // freed as the call ends
    ELG_TRY
    ELG_CATCH_VOID
}
/// Frees a `Binding<bool>` the caller owns, from elg_binding_bool().
///
/// @param binding The binding (consumed); may be null.
ELG_EXPORT void elg_binding_bool_free(Binding<bool>* binding) {
    const Consumed consumed(binding); // freed as the call ends
    ELG_TRY
    ELG_CATCH_VOID
}
/// Frees a `Binding<int>` the caller owns, from elg_binding_i32().
///
/// @param binding The binding (consumed); may be null.
ELG_EXPORT void elg_binding_i32_free(Binding<int>* binding) {
    const Consumed consumed(binding); // freed as the call ends
    ELG_TRY
    ELG_CATCH_VOID
}
/// Frees a `Binding<std::string>` the caller owns, from elg_binding_string().
///
/// @param binding The binding (consumed); may be null.
ELG_EXPORT void elg_binding_string_free(Binding<std::string>* binding) {
    const Consumed consumed(binding); // freed as the call ends
    ELG_TRY
    ELG_CATCH_VOID
}
/// Frees a `Binding<double>` the caller owns, from elg_binding_f64().
///
/// @param binding The binding (consumed); may be null.
ELG_EXPORT void elg_binding_f64_free(Binding<double>* binding) {
    const Consumed consumed(binding); // freed as the call ends
    ELG_TRY
    ELG_CATCH_VOID
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

/// An empty environment (`EnvironmentValues()`): every key reads its default.
///
/// @return The environment, owned by the caller (elg_env_free()). Null on failure.
ELG_EXPORT EnvironmentValues* elg_env_new() {
    ELG_TRY
    return new EnvironmentValues();
    ELG_CATCH(nullptr)
}

/// Copies an environment (cheap: copies share their entries).
///
/// @param environment The environment (borrowed).
/// @return The copy, owned by the caller. Null on failure.
ELG_EXPORT EnvironmentValues* elg_env_clone(const EnvironmentValues* environment) {
    ELG_TRY
    return new EnvironmentValues(*environment);
    ELG_CATCH(nullptr)
}

/// Frees an environment.
///
/// @param environment The environment (consumed); may be null.
ELG_EXPORT void elg_env_free(EnvironmentValues* environment) {
    const Consumed consumed(environment); // freed as the call ends
    ELG_TRY
    ELG_CATCH_VOID
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
/// @param result Receives the theme (borrowed); left as it was on failure.
ELG_EXPORT void elg_env_theme(const EnvironmentValues* environment, ElgTheme* result) {
    ELG_TRY
    cpp_theme(*result) = environment->get<ThemeKey>();
    ELG_CATCH_VOID
}

/// Wraps `system_accent_color()`: the accent color the person chose in the system's settings.
/// Reactive while an App runs (a tracked read, on the UI thread); without one, it asks the system
/// each time.
///
/// @param color Receives the color when there is one; left as it was otherwise.
/// @return Whether the system has an accent color; false on failure.
ELG_EXPORT bool elg_system_accent_color(ElgColor* color) {
    ELG_TRY
    const std::optional<Color> accent = system_accent_color();
    if (accent) {
        *color = c_color(*accent);
    }
    return accent.has_value();
    ELG_CATCH(false)
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
    const Consumed consumed(value);
    ELG_TRY
    environment->set<FontKey>(prop<Font>(value));
    ELG_CATCH_VOID
}
/// Sets the color scheme (ColorSchemeKey).
///
/// @param environment The environment to change (borrowed).
/// @param value The scheme: an ElgProp of elg::ColorScheme (kind 0: `int32_t*`, 0 light, 1 dark).
ELG_EXPORT void elg_env_set_color_scheme(EnvironmentValues* environment, ElgProp value) {
    const Consumed consumed(value);
    ELG_TRY
    environment->set<ColorSchemeKey>(prop<ColorScheme>(value));
    ELG_CATCH_VOID
}
/// Sets the theme (ThemeKey).
///
/// @param environment The environment to change (borrowed).
/// @param value The theme: an ElgProp of elg::Theme (kind 0: `ElgTheme*`).
ELG_EXPORT void elg_env_set_theme(EnvironmentValues* environment, ElgProp value) {
    const Consumed consumed(value);
    ELG_TRY
    environment->set<ThemeKey>(prop<Theme>(value));
    ELG_CATCH_VOID
}
/// Sets the foreground (ForegroundKey): the color of text and unfilled shapes.
///
/// @param environment The environment to change (borrowed).
/// @param value The color: an ElgProp of elg::Color (kind 0: `ElgColor*`; kind 3: a theme color).
ELG_EXPORT void elg_env_set_foreground(EnvironmentValues* environment, ElgProp value) {
    const Consumed consumed(value);
    ELG_TRY
    environment->set<ForegroundKey>(prop<Color>(value));
    ELG_CATCH_VOID
}
/// Sets the tint (TintKey): the accent color of controls.
///
/// @param environment The environment to change (borrowed).
/// @param value The color: an ElgProp of elg::Color (kind 0: `ElgColor*`; kind 3: a theme color).
ELG_EXPORT void elg_env_set_tint(EnvironmentValues* environment, ElgProp value) {
    const Consumed consumed(value);
    ELG_TRY
    environment->set<TintKey>(prop<Color>(value));
    ELG_CATCH_VOID
}
/// Sets DisabledKey: controls inside don't respond to input and look disabled.
///
/// @param environment The environment to change (borrowed).
/// @param value Whether they're disabled: an ElgProp of bool (kind 0: `bool*`).
ELG_EXPORT void elg_env_set_disabled(EnvironmentValues* environment, ElgProp value) {
    const Consumed consumed(value);
    ELG_TRY
    environment->set<DisabledKey>(prop<bool>(value));
    ELG_CATCH_VOID
}
/// Sets the hover delay (HoverDelayKey), for on_hover handlers without their own and controls.
///
/// @param environment The environment to change (borrowed).
/// @param value The delay: an ElgProp of elg::HoverDelay (kind 0: `ElgHoverDelay*`).
ELG_EXPORT void elg_env_set_hover_delay(EnvironmentValues* environment, ElgProp value) {
    const Consumed consumed(value);
    ELG_TRY
    environment->set<HoverDelayKey>(prop<HoverDelay>(value));
    ELG_CATCH_VOID
}
/// Sets RowSelectedKey: whether the views are in a selected row.
///
/// @param environment The environment to change (borrowed).
/// @param value Whether it's selected: an ElgProp of bool (kind 0: `bool*`).
ELG_EXPORT void elg_env_set_row_selected(EnvironmentValues* environment, ElgProp value) {
    const Consumed consumed(value);
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
    const Consumed consumed(values);
    ELG_TRY
    environment->set<RustEnvironmentKey>(hold(values));
    ELG_CATCH_VOID
}

// --- Windows ---

/// Copies a window handle; the copy refers to the same window.
///
/// @param window The handle (borrowed).
/// @return The copy, owned by the caller. Null on failure.
ELG_EXPORT WindowHandle* elg_window_clone(const WindowHandle* window) {
    ELG_TRY
    return new WindowHandle(*window);
    ELG_CATCH(nullptr)
}
/// Frees a window handle (not the window).
///
/// @param window The handle (consumed); may be null.
ELG_EXPORT void elg_window_free(WindowHandle* window) {
    const Consumed consumed(window); // freed as the call ends
    ELG_TRY
    ELG_CATCH_VOID
}
/// Whether two handles refer to the same window (`WindowHandle`'s operator==).
///
/// @param a A handle (borrowed).
/// @param b Another handle (borrowed).
/// @return true for the same window, or for two handles outside any window. False on failure.
ELG_EXPORT bool elg_window_eq(const WindowHandle* a, const WindowHandle* b) {
    ELG_TRY
    return *a == *b;
    ELG_CATCH(false)
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
    const Consumed consumed(app); // freed as the call ends
    ELG_TRY
    ELG_CATCH_VOID
}

/// Wraps `App::window()`: adds a window that opens when the app runs. `content` (ret: View*)
/// builds the window's root view when it opens. Records a UsageError while the app runs.
///
/// @param app The app (borrowed).
/// @param desc How the window opens (borrowed).
/// @param content Consumed closure, called once on the UI thread; args: null, ret: `View*` (set
///   it with elg_view_move_into()). Required: none is a usage error.
ELG_EXPORT void elg_app_window(App* app, const ElgWindowDesc* desc, ElgClosure content) {
    const Consumed consumed(content);
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
    const SharedFn f = required(content, "content");
    app->window(std::move(window), [f]() -> View {
        View result;
        f->call(nullptr, &result);
        return result;
    });
    ELG_CATCH_VOID
}

/// Wraps `App::run()`: opens the windows and runs the event loop until the last window closes or
/// elg_app_quit() is called. Errors and panics in app callbacks are written to the log and passed
/// to elg_app_on_error()'s handler, and the app goes on, unless the handler ends the run.
///
/// @param app The app (borrowed).
/// @return The process exit code, or -1 on failure (no windows, already running, an error the
///   error handler ended the run with, or one of Elegance's own).
ELG_EXPORT int32_t elg_app_run(App* app) {
    ELG_TRY
    return app->run();
    ELG_CATCH(-1)
}

/// Wraps `App::on_error()`: calls `handler` with each exception that escapes app code while the app
/// runs, binding callbacks included, on the UI thread between events.
///
/// @param app The app (borrowed).
/// @param handler Consumed closure; args: `const ElgAppError*`, valid during the call; ret:
///   `int32_t*`, which it sets to 1 to end the run (elg_app_run() then fails with that error) or
///   leaves 0 to go on. A null `call` removes the handler. What it panics with is only logged.
ELG_EXPORT void elg_app_on_error(App* app, ElgClosure handler) {
    const Consumed consumed(handler);
    ELG_TRY
    const SharedFn fn = share(handler);
    if (!fn) {
        app->on_error({});
        return;
    }
    app->on_error([fn](const AppError& error) {
        std::string message;
        const int32_t kind = elg_shim::error_kind(error.exception, message);
        ElgAppError info{kind, c_str(message), static_cast<int32_t>(error.code)};
        int32_t quit = 0;
        fn->call(&info, &quit);
        return quit == 1 ? ErrorAction::quit : ErrorAction::resume;
    });
    ELG_CATCH_VOID
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
///   Required: none is a usage error.
ELG_EXPORT void elg_post_to_ui_thread(ElgClosure task) {
    const Consumed consumed(task);
    ELG_TRY_ANY_THREAD
    post_to_ui_thread(action(required(task, "task")));
    ELG_CATCH_VOID
}

/// Wraps `version()`: the library's version string, e.g. "0.1.0".
/// Callable from any thread.
///
/// @return A static, NUL-terminated string; don't free it. Null on failure.
ELG_EXPORT const char* elg_version() {
    ELG_TRY_ANY_THREAD
    return version();
    ELG_CATCH(nullptr)
}

// The C API's ABI version. It goes up by one whenever an exported function's signature, a struct
// passed across the API, or the value of a documented constant changes.
constexpr std::uint32_t kAbiVersion = 4;

/// The C API's ABI version: a binding checks it when it loads the library, and refuses to run
/// with a version it wasn't built for. It changes whenever an exported function's signature, a
/// struct's layout, or a documented constant's value does.
/// Callable from any thread.
///
/// @return The version, 1 or more; 0 on failure.
ELG_EXPORT uint32_t elg_abi_version() {
    ELG_TRY_ANY_THREAD
    return kAbiVersion;
    ELG_CATCH(0)
}

// --- Animation ---

/// Wraps `Animation::linear()`: constant speed.
///
/// @param duration Seconds (the C++ default is 0.25); negative counts as 0.
/// @return The animation, owned by the caller (elg_animation_free()). Null on failure.
ELG_EXPORT Animation* elg_animation_linear(double duration) {
    ELG_TRY
    return new Animation(Animation::linear(duration));
    ELG_CATCH(nullptr)
}
/// Wraps `Animation::ease_in()`: starts slowly.
///
/// @param duration Seconds (the C++ default is 0.25); negative counts as 0.
/// @return The animation, owned by the caller (elg_animation_free()). Null on failure.
ELG_EXPORT Animation* elg_animation_ease_in(double duration) {
    ELG_TRY
    return new Animation(Animation::ease_in(duration));
    ELG_CATCH(nullptr)
}
/// Wraps `Animation::ease_out()`: ends slowly.
///
/// @param duration Seconds (the C++ default is 0.25); negative counts as 0.
/// @return The animation, owned by the caller (elg_animation_free()). Null on failure.
ELG_EXPORT Animation* elg_animation_ease_out(double duration) {
    ELG_TRY
    return new Animation(Animation::ease_out(duration));
    ELG_CATCH(nullptr)
}
/// Wraps `Animation::ease_in_out()`: starts and ends slowly.
///
/// @param duration Seconds (the C++ default is 0.35); negative counts as 0.
/// @return The animation, owned by the caller (elg_animation_free()). Null on failure.
ELG_EXPORT Animation* elg_animation_ease_in_out(double duration) {
    ELG_TRY
    return new Animation(Animation::ease_in_out(duration));
    ELG_CATCH(nullptr)
}
/// Wraps `Animation::spring()`: a spring settling at the target, like SwiftUI's
/// .spring(response:dampingFraction:).
///
/// @param response Roughly the time to get there, in seconds (the C++ default is 0.5).
/// @param damping_fraction 1 settles without overshoot; lower values bounce (the C++ default is
///   0.825).
/// @return The animation, owned by the caller (elg_animation_free()). Null on failure.
ELG_EXPORT Animation* elg_animation_spring(double response, double damping_fraction) {
    ELG_TRY
    return new Animation(Animation::spring(response, damping_fraction));
    ELG_CATCH(nullptr)
}
/// Wraps `Animation::smooth()`: a gentle spring without overshoot.
///
/// @param duration Roughly the time to get there, in seconds (the C++ default is 0.4).
/// @return The animation, owned by the caller (elg_animation_free()). Null on failure.
ELG_EXPORT Animation* elg_animation_smooth(double duration) {
    ELG_TRY
    return new Animation(Animation::smooth(duration));
    ELG_CATCH(nullptr)
}
/// Wraps `Animation::custom()`: `curve` (args: double* t, ret: double*) maps progress 0...1 to
/// eased progress. It may go past 0 or 1 to overshoot; the animation lands on the target once
/// `duration` is up regardless.
///
/// @param curve Consumed closure; args: `double*`, the fraction of `duration` elapsed (0 to 1);
///   ret: `double*`, the progress (0 at the start, 1 at the target), set to the fraction beforehand.
///   A panic in it propagates like one in any callback: from an animated property, out of
///   App::run; from elg_animation_progress(), as its error. Required: none is a usage error.
/// @param duration Seconds; negative counts as 0.
/// @return The animation, owned by the caller (elg_animation_free()), or nullptr on failure.
ELG_EXPORT Animation* elg_animation_custom(ElgClosure curve, double duration) {
    const Consumed consumed(curve);
    ELG_TRY
    const SharedFn f = required(curve, "curve");
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
/// @return The new animation, owned by the caller (elg_animation_free()). Null on failure.
ELG_EXPORT Animation* elg_animation_delay(const Animation* animation, double seconds) {
    ELG_TRY
    return new Animation(animation->delay(seconds));
    ELG_CATCH(nullptr)
}
/// Wraps `Animation::timing_curve()`: a cubic Bézier through (0, 0), (x1, y1), (x2, y2) and (1, 1), like
/// CSS's cubic-bezier().
///
/// @param x1 The first control point's time, 0 to 1 (clamped).
/// @param y1 The first control point's progress; outside 0 to 1 overshoots.
/// @param x2 The second control point's time, 0 to 1 (clamped).
/// @param y2 The second control point's progress; outside 0 to 1 overshoots.
/// @param duration Seconds; negative counts as 0 (no animation).
/// @return The animation, owned by the caller (elg_animation_free()). Null on failure.
ELG_EXPORT Animation* elg_animation_timing_curve(double x1, double y1, double x2, double y2, double duration) {
    ELG_TRY
    return new Animation(Animation::timing_curve(x1, y1, x2, y2, duration));
    ELG_CATCH(nullptr)
}
/// Wraps `Animation::repeat_count()`: a copy of `animation` that plays `count` times.
///
/// @param animation The animation (borrowed).
/// @param count How many times; less than 1 counts as 1.
/// @param autoreverses Every other time runs backward, from the target to where it started.
/// @return The new animation, owned by the caller (elg_animation_free()). Null on failure.
ELG_EXPORT Animation* elg_animation_repeat_count(const Animation* animation, int32_t count, bool autoreverses) {
    ELG_TRY
    return new Animation(animation->repeat_count(count, autoreverses));
    ELG_CATCH(nullptr)
}
/// Wraps `Animation::repeat_forever()`: a copy of `animation` that plays until it's replaced.
///
/// @param animation The animation (borrowed).
/// @param autoreverses Every other time runs backward, from the target to where it started.
/// @return The new animation, owned by the caller (elg_animation_free()). Null on failure.
ELG_EXPORT Animation* elg_animation_repeat_forever(const Animation* animation, bool autoreverses) {
    ELG_TRY
    return new Animation(animation->repeat_forever(autoreverses));
    ELG_CATCH(nullptr)
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
/// @return The copy, owned by the caller (elg_animation_free()). Null on failure.
ELG_EXPORT Animation* elg_animation_clone(const Animation* animation) {
    ELG_TRY
    return new Animation(*animation);
    ELG_CATCH(nullptr)
}
/// Frees an animation.
///
/// @param animation The animation (consumed); may be null.
ELG_EXPORT void elg_animation_free(Animation* animation) {
    const Consumed consumed(animation); // freed as the call ends
    ELG_TRY
    ELG_CATCH_VOID
}

// --- Timers ---

/// Wraps `after()`: runs `action` once, `seconds` from now (0: as soon as the current event is handled), on
/// the UI thread. A timer started while views are built, or in on_appear, ends with that view.
///
/// @param seconds The delay; negative counts as 0. Positive infinity never runs the action. NaN is a usage
///   error.
/// @param action The action. No args. May be none: the timer is never active.
/// @return The timer, owned by the caller (elg_timer_free(), which doesn't stop it). Null on failure.
ELG_EXPORT Timer* elg_timer_after(double seconds, ElgClosure action) {
    const Consumed consumed(action);
    ELG_TRY
    return new Timer(after(seconds, elg_shim::action(share(action))));
    ELG_CATCH(nullptr)
}
/// Wraps `every()`: runs `action` every `seconds` (at least a millisecond), the first time `seconds` from
/// now, on the UI thread. Missed runs are skipped. A timer started while views are built, or in
/// on_appear, ends with that view.
///
/// @param seconds The interval; less than a millisecond counts as a millisecond. Positive infinity never runs
///   the action. NaN is a usage error.
/// @param action The action. No args. May be none: the timer is never active.
/// @return The timer, owned by the caller (elg_timer_free(), which doesn't stop it). Null on failure.
ELG_EXPORT Timer* elg_timer_every(double seconds, ElgClosure action) {
    const Consumed consumed(action);
    ELG_TRY
    return new Timer(every(seconds, elg_shim::action(share(action))));
    ELG_CATCH(nullptr)
}
/// Wraps `Timer::cancel()`: the action doesn't run again. Fine to call more than once.
///
/// @param timer The timer (borrowed).
ELG_EXPORT void elg_timer_cancel(const Timer* timer) {
    ELG_TRY
    timer->cancel();
    ELG_CATCH_VOID
}
/// Wraps `Timer::active()`: whether the timer is still going: not cancelled, its view still there, and for
/// elg_timer_after(), not fired yet.
///
/// @param timer The timer (borrowed).
/// @return true while it's going; false on failure.
ELG_EXPORT bool elg_timer_active(const Timer* timer) {
    ELG_TRY
    return timer->active();
    ELG_CATCH(false)
}
/// Copies a timer handle: both refer to the same timer.
///
/// @param timer The timer (borrowed).
/// @return The copy, owned by the caller (elg_timer_free()). Null on failure.
ELG_EXPORT Timer* elg_timer_clone(const Timer* timer) {
    ELG_TRY
    return new Timer(*timer);
    ELG_CATCH(nullptr)
}
/// Frees a timer handle. The timer goes on running.
///
/// @param timer The timer (consumed); may be null.
ELG_EXPORT void elg_timer_free(Timer* timer) {
    const Consumed consumed(timer); // freed as the call ends
    ELG_TRY
    ELG_CATCH_VOID
}

// --- Transition ---

/// A Transition::Pose: how a view looks partway through a transition.
struct ElgPose {
    float opacity, scale, x, y; ///< Opacity and scale (1: as is), and the offset in points.
    float anchor_x, anchor_y;   ///< What it scales around, as fractions of its frame (0.5: the middle).
};

/// Wraps `Transition::opacity()`: the view fades in and out.
///
/// @return The transition, owned by the caller (elg_transition_free()). Null on failure.
ELG_EXPORT Transition* elg_transition_opacity() {
    ELG_TRY
    return new Transition(Transition::opacity());
    ELG_CATCH(nullptr)
}
/// Wraps `Transition::identity()`: the view appears and disappears at once.
///
/// @return The transition, owned by the caller (elg_transition_free()). Null on failure.
ELG_EXPORT Transition* elg_transition_identity() {
    ELG_TRY
    return new Transition(Transition::identity());
    ELG_CATCH(nullptr)
}
/// Wraps `Transition::scale()`: the view grows from (and shrinks to) `from` times its size.
///
/// @param from The scale it starts and ends at (the C++ default is 0).
/// @param anchor The point of its frame it scales around.
/// @return The transition, owned by the caller (elg_transition_free()). Null on failure.
ELG_EXPORT Transition* elg_transition_scale(float from, ElgAlignment anchor) {
    ELG_TRY
    return new Transition(Transition::scale(from, alignment(anchor)));
    ELG_CATCH(nullptr)
}
/// Wraps `Transition::offset()`: the view moves in from (and out to) an offset.
///
/// @param x Horizontal offset in points.
/// @param y Vertical offset in points.
/// @return The transition, owned by the caller (elg_transition_free()). Null on failure.
ELG_EXPORT Transition* elg_transition_offset(float x, float y) {
    ELG_TRY
    return new Transition(Transition::offset(x, y));
    ELG_CATCH(nullptr)
}
/// Wraps `Transition::move()`: the view slides in from (and out to) beyond an edge of its frame.
///
/// @param edge elg::Edges bits: 1 top, 2 leading, 4 bottom, 8 trailing.
/// @return The transition, owned by the caller (elg_transition_free()). Null on failure.
ELG_EXPORT Transition* elg_transition_move(uint32_t edge) {
    ELG_TRY
    return new Transition(Transition::move(static_cast<Edges>(edge)));
    ELG_CATCH(nullptr)
}
/// Wraps `Transition::slide()`: the view moves in from the leading edge and out to the trailing
/// edge.
///
/// @return The transition, owned by the caller (elg_transition_free()). Null on failure.
ELG_EXPORT Transition* elg_transition_slide() {
    ELG_TRY
    return new Transition(Transition::slide());
    ELG_CATCH(nullptr)
}
/// Wraps `Transition::asymmetric()`: different transitions for insertion and removal.
///
/// @param insertion How the view enters (borrowed).
/// @param removal How the view leaves (borrowed).
/// @return The transition, owned by the caller (elg_transition_free()). Null on failure.
ELG_EXPORT Transition* elg_transition_asymmetric(const Transition* insertion, const Transition* removal) {
    ELG_TRY
    return new Transition(Transition::asymmetric(*insertion, *removal));
    ELG_CATCH(nullptr)
}
/// Wraps `Transition::combined()`: both transitions at once.
///
/// @param a A transition (borrowed).
/// @param b Another transition (borrowed).
/// @return The transition, owned by the caller (elg_transition_free()). Null on failure.
ELG_EXPORT Transition* elg_transition_combined(const Transition* a, const Transition* b) {
    ELG_TRY
    return new Transition(a->combined(*b));
    ELG_CATCH(nullptr)
}
/// Wraps `Transition::pose()`: how the view looks at `progress`.
///
/// @param transition The transition (borrowed).
/// @param progress 0: fully out, 1: in place.
/// @param removal true for the view leaving, false for it entering.
/// @param view_size The view's size in points, for transitions that move it beyond an edge.
/// @return The pose. Zero on failure.
ELG_EXPORT ElgPose elg_transition_pose(const Transition* transition, float progress, bool removal, ElgSize view_size) {
    ELG_TRY
    const Transition::Pose pose = transition->pose(progress, removal, size(view_size));
    return {pose.opacity, pose.scale, pose.x, pose.y, pose.anchor_x, pose.anchor_y};
    ELG_CATCH({})
}
/// Copies a transition.
///
/// @param transition The transition (borrowed).
/// @return The copy, owned by the caller (elg_transition_free()). Null on failure.
ELG_EXPORT Transition* elg_transition_clone(const Transition* transition) {
    ELG_TRY
    return new Transition(*transition);
    ELG_CATCH(nullptr)
}
/// Frees a transition.
///
/// @param transition The transition (consumed); may be null.
ELG_EXPORT void elg_transition_free(Transition* transition) {
    const Consumed consumed(transition); // freed as the call ends
    ELG_TRY
    ELG_CATCH_VOID
}

// --- ABI layout (shim.h's structs and this file's) ---

/// Describes the layout of shim.h's structs and this file's, for a binding's layout test: one line
/// per struct, "name size align", then " field:offset" for each of its fields, in order.
///
/// @param out Receives each line.
/// @param ctx Passed to `out`.
ELG_EXPORT void elg_layouts_core(ElgStrSink out, void* ctx) {
    ELG_TRY
    ELG_LAYOUT(ElgStr);
    ELG_LAYOUT(ElgBytes);
    ELG_LAYOUT(ElgClosure);
    ELG_LAYOUT(ElgAppError);
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
    ELG_LAYOUT(ElgTheme);
    ELG_LAYOUT(ElgLayoutChild);
    ELG_LAYOUT(ElgWindowDesc);
    ELG_LAYOUT(ElgPose);
    ELG_CATCH_VOID
}
