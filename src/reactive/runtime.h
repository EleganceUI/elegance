// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_REACTIVE_RUNTIME_H
#define ELG_REACTIVE_RUNTIME_H

#include <functional>
#include <optional>
#include <thread>

namespace elg::detail {

class ReactiveNode;

// Runs `function` with `observer` as the reader: what it reads becomes `observer`'s dependencies,
// in place of the ones it had. For reads made outside a run of the observer, such as a canvas's
// drawing while it paints.
void track_reads(ReactiveNode& observer, const std::function<void()>& function);

// Pins reactive state to the App's UI thread while it runs (nullopt: no restriction).
void set_ui_thread(std::optional<std::thread::id> thread) noexcept;

} // namespace elg::detail

#endif // ELG_REACTIVE_RUNTIME_H
