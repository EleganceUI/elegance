// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_REACTIVE_H
#define ELG_REACTIVE_H

// Fine-grained reactive state (design.md §6): State, Binding, Computed, and effects.
//
//   auto count = State(0);
//   Text([=] { return std::format("Count: {}", count()); })   // re-renders that one Text
//   Button("+", [=] { count += 1; })
//
// Reading a State inside a closure that Elegance runs reactively (a reactive argument, a
// Computed, an effect) subscribes that closure; writing the State re-runs exactly the closures
// that read it, once per frame. Everything is UI-thread only.

#include <elg/error.h>
#include <elg/export.h>

#include <concepts>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

namespace elg {

class Animation;

namespace detail {

// A node in the dependency graph: a signal (State), a computed value, or an effect.
// The update algorithm pushes "maybe changed" marks down the graph on writes and pulls fresh
// values on reads, so every node recomputes at most once per change, never with half-updated
// inputs (glitch-free).
class ELG_API ReactiveNode {
public:
    virtual ~ReactiveNode();

    ReactiveNode(const ReactiveNode&) = delete;
    ReactiveNode& operator=(const ReactiveNode&) = delete;
    ReactiveNode(ReactiveNode&&) = delete;
    ReactiveNode& operator=(ReactiveNode&&) = delete;

    // A read: subscribes the running computed/effect to this node, and brings a computed up to
    // date first.
    void track();
    // A signal's value changed: marks dependents and schedules affected effects.
    void notify_changed();
    // Brings a computed or effect up to date, re-running it if any dependency changed.
    void update();
    // Unsubscribes from everything; a disposed effect never runs again.
    void dispose() noexcept;

protected:
    enum class Kind { signal, computed, effect };

    explicit ReactiveNode(Kind kind) noexcept;

    // Computed/effect: runs the user function. Returns true if a computed's value changed.
    virtual bool compute() { return false; }

private:
    friend struct Runtime;

    Kind kind_;
    int status_;
    bool queued_ = false;
    bool disposed_ = false;
    bool priority_ = false; // runs before other effects in a flush
    ELG_PRIVATE_MEMBERS_BEGIN
    std::vector<ReactiveNode*> sources_;
    std::vector<ReactiveNode*> observers_;
    // Effects: the animation of the transaction that scheduled the next run, if any.
    std::shared_ptr<const Animation> transaction_;
    ELG_PRIVATE_MEMBERS_END
};

template <class T>
class SignalNode final : public ReactiveNode {
public:
    explicit SignalNode(T initial) : ReactiveNode(Kind::signal), value(std::move(initial)) {}
    T value;
};

template <class T>
class ComputedNode final : public ReactiveNode {
public:
    explicit ComputedNode(std::function<T()> function) : ReactiveNode(Kind::computed), function_(std::move(function)) {}

    // Rethrows what the function threw, until something it read before throwing changes.
    const T& get() {
        track(); // runs the function if needed
        if (error_) {
            std::rethrow_exception(error_);
        }
        // NOLINTNEXTLINE(bugprone-unchecked-optional-access): once track() returns, error_ or value_ is set
        return *value_;
    }

protected:
    bool compute() override {
        std::optional<T> next;
        try {
            next.emplace(function_());
        } catch (...) {
            error_ = std::current_exception();
            throw;
        }
        // Recovering from an error is a change even to an equal value: readers that failed on it
        // run again.
        const bool recovered = std::exchange(error_, nullptr) != nullptr;
        if constexpr (std::equality_comparable<T>) {
            if (!recovered && value_ && *value_ == *next) {
                return false;
            }
        }
        value_ = std::move(next);
        return true;
    }

private:
    std::function<T()> function_;
    std::optional<T> value_;
    std::exception_ptr error_;
};

class EffectNode final : public ReactiveNode {
public:
    explicit EffectNode(std::function<void()> function) : ReactiveNode(Kind::effect), function_(std::move(function)) {}

protected:
    bool compute() override {
        function_();
        return false;
    }

private:
    std::function<void()> function_;
};

// Owns effects: disposes them when destroyed. The framework gives every mounted view tree a
// scope, so effects created while building views end with them (design.md §9.6, rule 3).
class ELG_API Scope {
public:
    Scope() = default;
    ~Scope();

    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
    Scope(Scope&&) = delete;
    Scope& operator=(Scope&&) = delete;

    void adopt(std::shared_ptr<ReactiveNode> node);
    // Runs `cleanup` when the scope ends, after its effects are disposed (timers stop, views
    // disappear). It must not run user code: queue that instead.
    void on_end(std::function<void()> cleanup);

    // Makes `scope` the current one while alive.
    class ELG_API Use {
    public:
        explicit Use(Scope& scope) noexcept;
        ~Use();
        Use(const Use&) = delete;
        Use& operator=(const Use&) = delete;
        Use(Use&&) = delete;
        Use& operator=(Use&&) = delete;

    private:
        Scope* previous_;
    };

private:
    ELG_PRIVATE_MEMBERS_BEGIN
    std::vector<std::shared_ptr<ReactiveNode>> nodes_;
    std::vector<std::function<void()>> cleanups_;
    ELG_PRIVATE_MEMBERS_END
};

ELG_API Scope* current_scope() noexcept;
// Adopts the effect into the current scope and runs it once. Throws UsageError without a scope.
// Priority effects run before other effects in a flush (implicit animation triggers).
ELG_API void start_effect(std::shared_ptr<EffectNode> effect, bool priority = false);

// Animated transactions (with_animation): effects scheduled by writes inside one run with
// current_animation() set to its animation. begin_transaction returns the transaction it
// replaces, which end_transaction restores, so transactions nest.
ELG_API std::shared_ptr<const Animation> begin_transaction(std::shared_ptr<const Animation> animation);
ELG_API void end_transaction(std::shared_ptr<const Animation> previous) noexcept;
ELG_API const Animation* current_animation() noexcept;
// Counts flushes; a flush's number lasts until the next flush starts (through layout).
ELG_API std::uint64_t current_flush() noexcept;
// The animation of the latest animated transaction that ran in a flush since the last call.
ELG_API std::shared_ptr<const Animation> take_flush_animation() noexcept;
// Throws UsageError when called off the UI thread of a running App.
ELG_API void check_ui_thread();
ELG_API void begin_batch() noexcept;
// Ends a batch; the outermost one runs the scheduled effects when `flush` is true.
ELG_API void end_batch(bool flush);
ELG_API void begin_untracked() noexcept;
ELG_API void end_untracked() noexcept;
// Runs scheduled effects. The framework calls this once per frame, before layout.
ELG_API void flush_effects();
// Error helpers for header templates.
[[noreturn]] ELG_API void throw_needs_environment();
[[noreturn]] ELG_API void throw_no_environment();

template <class F>
struct IsStdFunction : std::false_type {};
template <class Signature>
struct IsStdFunction<std::function<Signature>> : std::true_type {};

// Whether `function` is a callable that can be empty and is: an empty std::function, or a null
// function or member pointer. Lambdas, States and the like never are; other values aren't
// callables.
template <class F>
bool is_empty_function(const F& function) noexcept {
    if constexpr ((std::is_pointer_v<F> && std::is_function_v<std::remove_pointer_t<F>>) ||
                  std::is_member_pointer_v<F>) {
        return function == nullptr;
    } else if constexpr (IsStdFunction<F>::value) {
        return !function;
    } else {
        return false;
    }
}

// `function`, passed on; throws UsageError with `message` if it's empty (ELGSPEC-0010.R1).
template <class F>
F&& non_empty(F&& function, const char* message) {
    if (is_empty_function(function)) {
        throw UsageError(message);
    }
    return std::forward<F>(function);
}

} // namespace detail

template <class T>
class Binding;

/// A piece of mutable state: a cheap handle to a shared value. Copies refer to the same value,
/// so lambdas can capture it by value and still write to it. UI thread only: other threads hand
/// their results over with post_to_ui_thread().
///
/// @code
/// auto count = State(0);
/// Text([=] { return std::format("Count: {}", count()); })   // re-renders that one Text
/// Button("+", [=] { count += 1; })
/// @endcode
///
/// @tparam T The value's type. Equality-comparable types skip writes of an equal value.
template <class T>
class State {
public:
    /// A new piece of state. Explicit, so `count = 1` assigns the value rather than a new State.
    ///
    /// @param initial The starting value.
    explicit State(T initial = T{}) : node_(std::make_shared<detail::SignalNode<T>>(std::move(initial))) {}

    /// Reads the value, subscribing the running computed/effect/reactive argument.
    ///
    /// @return The current value.
    const T& operator()() const {
        node_->track();
        return node_->value;
    }
    /// Like operator()(): reads the value, subscribing the running computed/effect/reactive
    /// argument.
    ///
    /// @return The current value.
    [[nodiscard]] const T& get() const { return (*this)(); }
    /// Reads without subscribing.
    ///
    /// @return The current value.
    [[nodiscard]] const T& peek() const noexcept { return node_->value; }

    /// Writes the value. What read it runs again (effects after the current event, before the
    /// next frame). For an equality-comparable T, writing a value equal to the current one does
    /// nothing.
    ///
    /// @param value The new value.
    /// @throws UsageError Off the UI thread of a running App.
    void set(T value) const {
        detail::check_ui_thread();
        if constexpr (std::equality_comparable<T>) {
            if (node_->value == value) {
                return;
            }
        }
        node_->value = std::move(value);
        node_->notify_changed();
    }

    /// Changes the value in place: `count.update([](int& c) { c += 2; })`. Always counts as a
    /// change, even when `change` leaves the value as it was.
    ///
    /// @param change Gets the value by reference and modifies it.
    /// @throws UsageError Off the UI thread of a running App.
    template <std::invocable<T&> F>
    void update(F&& change) const {
        detail::check_ui_thread();
        std::invoke(std::forward<F>(change), node_->value);
        node_->notify_changed();
    }

    /// Assigns the value, not the handle, so it's const (and works on a const State). Same as
    /// set().
    ///
    /// @param value The new value.
    /// @return This State.
    const State& operator=(T value) const { // NOLINT(misc-unconventional-assign-operator,cppcoreguidelines-c-copy-assignment-signature)
        set(std::move(value));
        return *this;
    }

    /// Adds to the value in place, as update() does: `count += 1`.
    ///
    /// @param amount What to add.
    /// @return This State.
    template <class U>
        requires requires(T& t, const U& u) { t += u; }
    const State& operator+=(const U& amount) const {
        update([&](T& value) { value += amount; });
        return *this;
    }

    /// Subtracts from the value in place, as update() does: `count -= 1`.
    ///
    /// @param amount What to subtract.
    /// @return This State.
    template <class U>
        requires requires(T& t, const U& u) { t -= u; }
    const State& operator-=(const U& amount) const {
        update([&](T& value) { value -= amount; });
        return *this;
    }

    /// A two-way connection to this state, for passing to child views.
    ///
    /// @return A Binding that reads (tracked) and writes this State.
    [[nodiscard]] Binding<T> binding() const;

private:
    std::shared_ptr<detail::SignalNode<T>> node_;
};

/// Read/write access to a value owned elsewhere (usually a State). Child components take
/// Bindings so they can change their parent's state. Copies share the getter and setter.
///
/// @tparam T The value's type.
template <class T>
class Binding {
public:
    /// A binding through your own getter and setter, e.g. to one field of a State.
    ///
    /// @param get Returns the value. Reads of state inside it are tracked.
    /// @param set Writes a new value.
    /// @throws UsageError if `get` or `set` is empty.
    Binding(std::function<T()> get, std::function<void(T)> set)
        : get_(detail::non_empty(std::move(get), "Binding needs a getter")),
          set_(detail::non_empty(std::move(set), "Binding needs a setter")) {}
    /// A binding to `state` (implicit, so a State goes wherever a Binding does).
    ///
    /// @param state The state to read and write; the binding keeps a handle to it.
    Binding(const State<T>& state)
        : get_([state] { return state(); }), set_([state](T value) { state.set(std::move(value)); }) {}

    /// A binding to a fixed value; writes are ignored. Handy for previews.
    ///
    /// @param value The value it always reads.
    /// @return The binding.
    static Binding constant(const T& value) {
        return Binding([value] { return value; }, [](const T&) {});
    }

    /// Reads the value through the getter (a tracked read, for a binding to a State).
    ///
    /// @return The value.
    T operator()() const { return get_(); }
    /// Like operator()(): reads the value through the getter.
    ///
    /// @return The value.
    [[nodiscard]] T get() const { return get_(); }
    /// Writes the value through the setter.
    ///
    /// @param value The new value.
    void set(T value) const { set_(std::move(value)); }

    /// Assigns the value, not the handle, so it's const (and works on a const Binding). Same as
    /// set().
    ///
    /// @param value The new value.
    /// @return This Binding.
    const Binding& operator=(T value) const { // NOLINT(misc-unconventional-assign-operator,cppcoreguidelines-c-copy-assignment-signature)
        set(std::move(value));
        return *this;
    }

    /// Reads the value, adds to it and writes it back: `volume += 0.1f`.
    ///
    /// @param amount What to add.
    /// @return This Binding.
    template <class U>
        requires requires(T& t, const U& u) { t += u; }
    const Binding& operator+=(const U& amount) const {
        T value = get_();
        value += amount;
        set(std::move(value));
        return *this;
    }

    /// Reads the value, subtracts from it and writes it back: `volume -= 0.1f`.
    ///
    /// @param amount What to subtract.
    /// @return This Binding.
    template <class U>
        requires requires(T& t, const U& u) { t -= u; }
    const Binding& operator-=(const U& amount) const {
        T value = get_();
        value -= amount;
        set(std::move(value));
        return *this;
    }

private:
    std::function<T()> get_;
    std::function<void(T)> set_;
};

template <class T>
Binding<T> State<T>::binding() const {
    return Binding<T>(*this);
}

/// A binding to a fixed value, for a control that should show `value` and ignore edits:
///
/// @code
/// Toggle("Always on", constant(true))
/// @endcode
///
/// @param value The value it always reads (copied).
/// @return A Binding that reads `value` and ignores writes.
template <class T>
Binding<std::decay_t<T>> constant(T&& value) {
    return Binding<std::decay_t<T>>::constant(std::forward<T>(value));
}

/// Read-only access to a value that can change: a State, Binding, Computed, or closure. Reading it
/// is a tracked read. What a style gets for a control's state (is_hovered, is_on, ...); pass it
/// anywhere a Prop goes.
///
/// @tparam T The value's type.
template <class T>
class Signal {
public:
    /// Wraps anything callable that returns a T (or something convertible to it).
    ///
    /// @param function A State, Binding, Computed or closure; it's copied (or moved) in.
    /// @throws UsageError if `function` is empty (an empty std::function or a null pointer).
    template <class F>
        requires(!std::same_as<std::remove_cvref_t<F>, Signal> && std::invocable<const std::remove_cvref_t<F>&> &&
                 std::convertible_to<std::invoke_result_t<const std::remove_cvref_t<F>&>, T>)
    // NOLINTNEXTLINE(cppcoreguidelines-missing-std-forward): forwarded into the capture
    Signal(F&& function)
        : get_([f = std::remove_cvref_t<F>(detail::non_empty(std::forward<F>(function), "Signal needs a function"))] {
              return T(f());
          }) {}

    /// Reads the value. A tracked read.
    ///
    /// @return The value.
    T operator()() const { return get_(); }
    /// Like operator()(): reads the value. A tracked read.
    ///
    /// @return The value.
    [[nodiscard]] T get() const { return get_(); }

private:
    std::function<T()> get_;
};

/// A value derived from state: recomputed lazily, at most once per change, and only when read.
/// Copies share the value. Usually made with computed().
///
/// @tparam T The value's type. When it's equality-comparable, a recomputed value equal to the
///   last one doesn't count as a change for what reads it.
template <class T>
class Computed {
public:
    /// A computed value; `function` first runs when it's read.
    ///
    /// @param function Computes the value from state; the state it reads is tracked.
    /// @throws UsageError if `function` is empty.
    explicit Computed(std::function<T()> function)
        : node_(std::make_shared<detail::ComputedNode<T>>(
              detail::non_empty(std::move(function), "Computed needs a function"))) {}

    /// Reads the value, recomputing it first if state it read changed. A tracked read. If
    /// `function` threw, reads rethrow that until something it read before throwing changes.
    ///
    /// @return The value.
    const T& operator()() const { return node_->get(); }
    /// Like operator()(): reads the value, recomputing it first if needed. A tracked read, which
    /// rethrows what `function` threw as operator()() does.
    ///
    /// @return The value.
    [[nodiscard]] const T& get() const { return node_->get(); }

private:
    std::shared_ptr<detail::ComputedNode<T>> node_;
};

/// Makes a Computed from a closure, deducing its type:
///
/// @code
/// auto doubled = computed([=] { return count() * 2; });
/// @endcode
///
/// @param function Computes the value from state; first runs when the value is read.
/// @return The Computed.
/// @throws UsageError if `function` is empty (an empty std::function or a null pointer).
template <std::invocable F>
auto computed(F function) -> Computed<std::decay_t<std::invoke_result_t<F>>> {
    using T = std::decay_t<std::invoke_result_t<F>>;
    return Computed<T>(std::function<T()>(std::move(function)));
}

/// Runs `function` now and again whenever state it read changes (after the current event, before
/// the next frame). Call it while building views; it ends when those views are removed.
///
/// @param function The effect. Each run tracks the state it reads anew.
/// @throws UsageError When `function` is empty (an empty std::function or a null pointer), when
///   no views are being built (nothing would end the effect), or off the UI thread of a running
///   App.
template <std::invocable F>
void effect(F function) {
    std::function<void()> body(std::move(function));
    detail::start_effect(
        std::make_shared<detail::EffectNode>(detail::non_empty(std::move(body), "effect() needs a function")));
}

/// Reads state inside `function` without subscribing to it. Effects created inside still track
/// their own reads.
///
/// @param function Runs once, now.
/// @return What `function` returns.
template <std::invocable F>
decltype(auto) untrack(F&& function) {
    detail::begin_untracked();
    struct End {
        End() = default;
        End(const End&) = delete;
        End(End&&) = delete;
        End& operator=(const End&) = delete;
        End& operator=(End&&) = delete;
        ~End() { detail::end_untracked(); }
    };
    const End end;
    return std::invoke(std::forward<F>(function));
}

/// Groups several writes: effects run once, after the outermost batch ends. The outermost batch
/// runs them right away, rather than before the next frame.
///
/// @param function Runs once, now. If it throws, the effects stay scheduled and the exception
///   propagates.
template <std::invocable F>
void batch(F&& function) {
    detail::begin_batch();
    try {
        std::invoke(std::forward<F>(function));
    } catch (...) {
        detail::end_batch(false);
        throw;
    }
    detail::end_batch(true);
}

} // namespace elg

#endif // ELG_REACTIVE_H
