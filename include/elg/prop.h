// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_PROP_H
#define ELG_PROP_H

#include <elg/export.h>
#include <elg/reactive.h>
#include <elg/theme.h>

#include <concepts>
#include <functional>
#include <type_traits>
#include <utility>
#include <variant>

namespace elg {

class EnvironmentValues;

namespace detail {
// The token's color in `environment`'s theme and color scheme (a tracked read).
ELG_API Color resolve_theme_color(const EnvironmentValues& environment, ThemeColor token);
} // namespace detail

// A reactive argument (design.md §9.3): a plain value, or anything that produces one:
//
//   .opacity(0.5f)                                   // static
//   .opacity(fade)                                   // State<float>
//   .opacity([=] { return hovered() ? 1.f : 0.6f; }) // derived
//   .fill(ThemeColor::accent)                        // resolved against the environment
//
// A reactive argument becomes one effect that updates one property of one element, so only that
// property changes when the state it reads does.
template <class T>
class Prop {
public:
    Prop(T value) : value_(std::move(value)) {}

    // Values convertible to T, e.g. "text" for Prop<std::string> or a Color for
    // Prop<ShapeStyle>.
    template <class U>
        requires(!std::same_as<std::remove_cvref_t<U>, T> && !std::same_as<std::remove_cvref_t<U>, Prop> &&
                 !std::same_as<std::remove_cvref_t<U>, ThemeColor> && std::convertible_to<U, T> &&
                 !std::invocable<U>)
    Prop(U&& value) : value_(T(std::forward<U>(value))) {}

    // Closures, States, Bindings, Computeds: anything callable that returns a T (or something
    // convertible to it).
    template <class F>
        requires(!std::same_as<std::remove_cvref_t<F>, Prop> && std::invocable<const std::remove_cvref_t<F>&> &&
                 std::convertible_to<std::invoke_result_t<const std::remove_cvref_t<F>&>, T>)
    Prop(F&& function)
        : value_(Reactive([f = std::remove_cvref_t<F>(std::forward<F>(function))] { return T(f()); })) {}

    // Closures that need the environment where the prop is used, e.g. to mix state with theme
    // colors: [=](const EnvironmentValues& env) { return on() ? accent : env.color(ThemeColor::outline); }
    template <class F>
        requires(!std::same_as<std::remove_cvref_t<F>, Prop> &&
                 std::invocable<const std::remove_cvref_t<F>&, const EnvironmentValues&> &&
                 std::convertible_to<std::invoke_result_t<const std::remove_cvref_t<F>&, const EnvironmentValues&>, T>)
    Prop(F&& function)
        : value_(FromEnvironment([f = std::remove_cvref_t<F>(std::forward<F>(function))](
                                     const EnvironmentValues& environment) { return T(f(environment)); })) {}

    // Semantic theme colors, for any T a Color converts to (Color, ShapeStyle).
    Prop(ThemeColor token)
        requires std::convertible_to<Color, T>
        : value_(FromEnvironment([token](const EnvironmentValues& environment) {
              return T(detail::resolve_theme_color(environment, token));
          })) {}

    bool is_reactive() const noexcept { return value_.index() != 0; }
    // Whether the value depends on the environment it's used in (e.g. a ThemeColor).
    bool needs_environment() const noexcept { return value_.index() == 2; }
    // The value if it's static, else nullptr.
    const T* static_value() const noexcept { return std::get_if<T>(&value_); }

    // The current value; a tracked read for reactive props. `environment` resolves
    // environment-dependent values; the framework passes the one where the prop is used.
    T get(const EnvironmentValues& environment) const {
        switch (value_.index()) {
        case 0: return std::get<0>(value_);
        case 1: return std::get<1>(value_)();
        default: return std::get<2>(value_)(environment);
        }
    }

    // As above, for props that don't need an environment (throws UsageError if this one does).
    T get() const {
        if (value_.index() == 2) {
            detail::throw_needs_environment();
        }
        return value_.index() == 0 ? std::get<0>(value_) : std::get<1>(value_)();
    }

    // Binds an environment-dependent prop to `environment`, producing a prop that no longer
    // needs one (other props are returned unchanged).
    Prop resolved_in(const EnvironmentValues& environment) const;

private:
    using Reactive = std::function<T()>;
    using FromEnvironment = std::function<T(const EnvironmentValues&)>;

    struct Tag {};
    explicit Prop(Tag, Reactive function) : value_(std::move(function)) {}

    std::variant<T, Reactive, FromEnvironment> value_;
};

} // namespace elg

#endif // ELG_PROP_H
