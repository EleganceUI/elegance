// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_REACTIVE_RUNTIME_H
#define ELG_REACTIVE_RUNTIME_H

#include <optional>
#include <thread>

namespace elg::detail {

// Pins reactive state to the App's UI thread while it runs (nullopt: no restriction).
void set_ui_thread(std::optional<std::thread::id> thread) noexcept;

} // namespace elg::detail

#endif // ELG_REACTIVE_RUNTIME_H
