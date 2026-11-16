// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Core's backend (core/os_backend.h) on Windows. Its standard library has <stacktrace>, so the
// stack functions aren't needed here.

#include "core/os_backend.h"

#include <windows.h>

namespace elg::detail::os {

bool debugger_present() noexcept {
    return IsDebuggerPresent() != 0;
}

void debug_break() noexcept {
    __debugbreak();
}

} // namespace elg::detail::os
