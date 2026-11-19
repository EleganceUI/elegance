// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include "core/threads.h"

#include "core/os_backend.h"

namespace elg::detail {

ThreadSetup::ThreadSetup() noexcept : begun_(os::begin_elegance_thread()) {}

ThreadSetup::~ThreadSetup() {
    os::end_elegance_thread(begun_);
}

} // namespace elg::detail
