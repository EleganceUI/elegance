// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_CORE_SCOPE_EXIT_H
#define ELG_CORE_SCOPE_EXIT_H

#include <utility>

namespace elg::detail {

// Runs a callable when the scope ends, including during exception unwinding.
template <class F>
class ScopeExit {
public:
    explicit ScopeExit(F fn) : fn_(std::move(fn)) {}
    ~ScopeExit() { fn_(); }

    ScopeExit(const ScopeExit&) = delete;
    ScopeExit& operator=(const ScopeExit&) = delete;

private:
    F fn_;
};

} // namespace elg::detail

#endif // ELG_CORE_SCOPE_EXIT_H
