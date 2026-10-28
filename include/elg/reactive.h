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

#include <elg/export.h>

#include <concepts>
#include <cstdint>
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

    const T& get() {
        track(); // runs the function if needed
        return *value_;
    }

protected:
    bool compute() override {
        T next = function_();
        if constexpr (std::equality_comparable<T>) {
            if (value_ && *value_ == next) {
                return false;
            }
        }
        value_ = std::move(next);
        return true;
    }

private:
    std::function<T()> function_;
    std::optional<T> value_;
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

    void adopt(std::shared_ptr<ReactiveNode> node);

    // Makes `scope` the current one while alive.
    class ELG_API Use {
    public:
        explicit Use(Scope& scope) noexcept;
        ~Use();
        Use(const Use&) = delete;
        Use& operator=(const Use&) = delete;

    private:
        Scope* previous_;
    };

private:
    ELG_PRIVATE_MEMBERS_BEGIN
    std::vector<std::shared_ptr<ReactiveNode>> nodes_;
    ELG_PRIVATE_MEMBERS_END
};

ELG_API Scope* current_scope() noexcept;
// Adopts the effect into the current scope and runs it once. Throws UsageError without a scope.
// Priority effects run before other effects in a flush (implicit animation triggers).
ELG_API void start_effect(std::shared_ptr<EffectNode> effect, bool priority = false);

// Animated transactions (with_animation): effects scheduled by writes inside one run with
// current_animation() set to its animation.
ELG_API void begin_transaction(std::shared_ptr<const Animation> animation);
ELG_API void end_transaction() noexcept;
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

} // namespace detail

template <class T>
class Binding;

// A piece of mutable state: a cheap handle to a shared value. Copies refer to the same value,
// so lambdas can capture it by value and still write to it.
template <class T>
class State {
public:
    // Explicit, so `count = 1` assigns the value rather than a new State.
    explicit State(T initial = T{}) : node_(std::make_shared<detail::SignalNode<T>>(std::move(initial))) {}

    // Reads the value, subscribing the running computed/effect/reactive argument.
    const T& operator()() const {
        node_->track();
        return node_->value;
    }
    const T& get() const { return (*this)(); }
    // Reads without subscribing.
    const T& peek() const noexcept { return node_->value; }

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

    // Changes the value in place: count.update([](int& c) { c += 2; })
    template <std::invocable<T&> F>
    void update(F&& change) const {
        detail::check_ui_thread();
        std::invoke(std::forward<F>(change), node_->value);
        node_->notify_changed();
    }

    const State& operator=(T value) const {
        set(std::move(value));
        return *this;
    }

    template <class U>
        requires requires(T& t, const U& u) { t += u; }
    const State& operator+=(const U& amount) const {
        update([&](T& value) { value += amount; });
        return *this;
    }

    template <class U>
        requires requires(T& t, const U& u) { t -= u; }
    const State& operator-=(const U& amount) const {
        update([&](T& value) { value -= amount; });
        return *this;
    }

    // A two-way connection to this state, for passing to child views.
    Binding<T> binding() const;

private:
    std::shared_ptr<detail::SignalNode<T>> node_;
};

// Read/write access to a value owned elsewhere (usually a State). Child components take
// Bindings so they can change their parent's state.
template <class T>
class Binding {
public:
    Binding(std::function<T()> get, std::function<void(T)> set) : get_(std::move(get)), set_(std::move(set)) {}
    Binding(const State<T>& state)
        : get_([state] { return state(); }), set_([state](T value) { state.set(std::move(value)); }) {}

    // A binding to a fixed value; writes are ignored. Handy for previews.
    static Binding constant(T value) {
        return Binding([value] { return value; }, [](T) {});
    }

    T operator()() const { return get_(); }
    T get() const { return get_(); }
    void set(T value) const { set_(std::move(value)); }

    const Binding& operator=(T value) const {
        set(std::move(value));
        return *this;
    }

    template <class U>
        requires requires(T& t, const U& u) { t += u; }
    const Binding& operator+=(const U& amount) const {
        T value = get_();
        value += amount;
        set(std::move(value));
        return *this;
    }

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

// A binding to a fixed value, for a control that should show `value` and ignore edits:
//   Toggle("Always on", constant(true))
template <class T>
Binding<std::decay_t<T>> constant(T&& value) {
    return Binding<std::decay_t<T>>::constant(std::forward<T>(value));
}

// Read-only access to a value that can change: a State, Binding, Computed, or closure. Reading it
// is a tracked read. What a style gets for a control's state (is_hovered, is_on, ...); pass it
// anywhere a Prop goes.
template <class T>
class Signal {
public:
    template <class F>
        requires(!std::same_as<std::remove_cvref_t<F>, Signal> && std::invocable<const std::remove_cvref_t<F>&> &&
                 std::convertible_to<std::invoke_result_t<const std::remove_cvref_t<F>&>, T>)
    Signal(F&& function) : get_([f = std::remove_cvref_t<F>(std::forward<F>(function))] { return T(f()); }) {}

    T operator()() const { return get_(); }
    T get() const { return get_(); }

private:
    std::function<T()> get_;
};

// A value derived from state: recomputed lazily, at most once per change, and only when read.
template <class T>
class Computed {
public:
    explicit Computed(std::function<T()> function)
        : node_(std::make_shared<detail::ComputedNode<T>>(std::move(function))) {}

    const T& operator()() const { return node_->get(); }
    const T& get() const { return node_->get(); }

private:
    std::shared_ptr<detail::ComputedNode<T>> node_;
};

// auto doubled = computed([=] { return count() * 2; });
template <std::invocable F>
auto computed(F function) -> Computed<std::decay_t<std::invoke_result_t<F>>> {
    using T = std::decay_t<std::invoke_result_t<F>>;
    return Computed<T>(std::function<T()>(std::move(function)));
}

// Runs `function` now and again whenever state it read changes (after the current event, before
// the next frame). Call it while building views; it ends when those views are removed.
template <std::invocable F>
void effect(F function) {
    detail::start_effect(std::make_shared<detail::EffectNode>(std::function<void()>(std::move(function))));
}

// Reads state inside `function` without subscribing to it.
template <std::invocable F>
decltype(auto) untrack(F&& function) {
    detail::begin_untracked();
    struct End {
        ~End() { detail::end_untracked(); }
    } end;
    return std::invoke(std::forward<F>(function));
}

// Groups several writes: effects run once, after the outermost batch ends.
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
