// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include "core/child_process.h"

#include "core/os_backend.h"

#include <elg/error.h>

#include <format>

namespace elg::detail {

ChildProcess::ChildProcess(const std::string& command) : handle_(os::start_command(command)) {
    if (!handle_) {
        throw ResourceError(std::format("can't start \"{}\"", command));
    }
}

ChildProcess::~ChildProcess() {
    if (handle_) {
        os::end_command(handle_);
        os::finish_command(handle_);
    }
}

std::size_t ChildProcess::read(std::span<char> buffer) {
    return handle_ ? os::read_command_output(handle_, buffer) : 0;
}

void ChildProcess::end() noexcept {
    if (handle_) {
        os::end_command(handle_);
    }
}

int ChildProcess::wait() noexcept {
    if (!handle_) {
        return -1;
    }
    const int status = os::finish_command(handle_);
    handle_ = nullptr;
    return status;
}

} // namespace elg::detail
