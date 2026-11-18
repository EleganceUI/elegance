// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_CORE_CHILD_PROCESS_H
#define ELG_CORE_CHILD_PROCESS_H

#include <cstddef>
#include <span>
#include <string>

namespace elg::detail {

// A command run in the system's shell as a child process (core/os_backend.h), its standard error
// joined to its standard output. On Linux and macOS, end() kills it and every process it started,
// without waiting for them; on Windows, end() does nothing yet. Destroying it ends it, then waits
// for its shell.
class ChildProcess {
public:
    // Throws elg::ResourceError if it can't be started.
    explicit ChildProcess(const std::string& command);
    ~ChildProcess();

    ChildProcess(const ChildProcess&) = delete;
    ChildProcess& operator=(const ChildProcess&) = delete;
    ChildProcess(ChildProcess&&) = delete;
    ChildProcess& operator=(ChildProcess&&) = delete;

    // Reads its output into `buffer`, waiting until there's some: how many bytes were read, 0 once
    // the output has ended or end() was called.
    std::size_t read(std::span<char> buffer);
    // Ends it; a read() waiting on another thread returns 0. Any thread, but not at the same time
    // as wait() or the destructor.
    void end() noexcept;
    // Waits for its shell to exit: the shell's exit status, or -1 if it didn't exit by itself
    // (end() ended it). Once only.
    int wait() noexcept;

private:
    void* handle_ = nullptr;
};

} // namespace elg::detail

#endif // ELG_CORE_CHILD_PROCESS_H
