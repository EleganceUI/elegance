// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_PROP_H
#define ELG_PROP_H

#include <elg/reactive.h>

#include <concepts>
#include <functional>
#include <type_traits>
#include <utility>
#include <variant>

namespace elg {

// A reactive argument (design.md §9.3): a plain value, or anything that produces one when
// called, such as a closure, a State, or a Computed:
//
//   .opacity(0.5f)                                   // static
//   .opacity(fade)                                   // State<float>
//   .opacity([=] { return hovered() ? 1.f : 0.6f; }) // derived
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
                 std::convertible_to<U, T> && !std::invocable<U>)
    Prop(U&& value) : value_(T(std::forward<U>(value))) {}

    // Closures, States, Bindings, Computeds: anything callable that returns a T (or something
    // convertible to it).
    template <class F>
        requires(!std::same_as<std::remove_cvref_t<F>, Prop> && std::invocable<const std::remove_cvref_t<F>&> &&
                 std::convertible_to<std::invoke_result_t<const std::remove_cvref_t<F>&>, T>)
    Prop(F&& function)
        : value_(std::function<T()>([f = std::remove_cvref_t<F>(std::forward<F>(function))] { return T(f()); })) {}

    bool is_reactive() const noexcept { return value_.index() == 1; }
    // The value if it's static, else nullptr.
    const T* static_value() const noexcept { return std::get_if<T>(&value_); }
    // The current value; a tracked read for reactive props.
    T get() const {
        if (const T* value = std::get_if<T>(&value_)) {
            return *value;
        }
        return std::get<1>(value_)();
    }

private:
    std::variant<T, std::function<T()>> value_;
};

} // namespace elg

#endif // ELG_PROP_H
