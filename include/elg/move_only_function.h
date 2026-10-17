// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_MOVE_ONLY_FUNCTION_H
#define ELG_MOVE_ONLY_FUNCTION_H

#include <functional>
#include <memory>
#include <type_traits>
#include <utility>
#include <version>

namespace elg {

#if defined(__cpp_lib_move_only_function)

template <typename Signature>
using MoveOnlyFunction = std::move_only_function<Signature>;

#else

// std::move_only_function for standard libraries without it (libc++, which macOS uses). Only
// the plain `R(Args...)` form, which is all Elegence needs.
template <typename Signature>
class MoveOnlyFunction;

template <typename R, typename... Args>
class MoveOnlyFunction<R(Args...)> {
public:
    MoveOnlyFunction() noexcept = default;
    MoveOnlyFunction(std::nullptr_t) noexcept {}

    template <typename F>
        requires(!std::is_same_v<std::remove_cvref_t<F>, MoveOnlyFunction> &&
                 std::is_invocable_r_v<R, std::decay_t<F>&, Args...>)
    MoveOnlyFunction(F&& function) {
        using Stored = std::decay_t<F>;
        if constexpr (std::is_pointer_v<Stored> || std::is_member_pointer_v<Stored>) {
            if (function == nullptr) {
                return;
            }
        }
        callable_ = std::make_unique<Model<Stored>>(std::forward<F>(function));
    }

    MoveOnlyFunction(MoveOnlyFunction&&) noexcept = default;
    MoveOnlyFunction& operator=(MoveOnlyFunction&&) noexcept = default;
    MoveOnlyFunction(const MoveOnlyFunction&) = delete;
    MoveOnlyFunction& operator=(const MoveOnlyFunction&) = delete;

    MoveOnlyFunction& operator=(std::nullptr_t) noexcept {
        callable_.reset();
        return *this;
    }

    template <typename F>
        requires(!std::is_same_v<std::remove_cvref_t<F>, MoveOnlyFunction>)
    MoveOnlyFunction& operator=(F&& function) {
        MoveOnlyFunction(std::forward<F>(function)).swap(*this);
        return *this;
    }

    R operator()(Args... args) { return callable_->call(std::forward<Args>(args)...); }

    explicit operator bool() const noexcept { return callable_ != nullptr; }
    friend bool operator==(const MoveOnlyFunction& f, std::nullptr_t) noexcept { return !f; }

    void swap(MoveOnlyFunction& other) noexcept { callable_.swap(other.callable_); }

private:
    struct Concept {
        virtual ~Concept() = default;
        virtual R call(Args&&... args) = 0;
    };
    template <typename F>
    struct Model final : Concept {
        template <typename G>
        explicit Model(G&& f) : function(std::forward<G>(f)) {}
        R call(Args&&... args) override { return std::invoke_r<R>(function, std::forward<Args>(args)...); }
        F function;
    };

    std::unique_ptr<Concept> callable_;
};

#endif

} // namespace elg

#endif // ELG_MOVE_ONLY_FUNCTION_H
