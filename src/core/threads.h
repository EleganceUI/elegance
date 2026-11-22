// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_CORE_THREADS_H
#define ELG_CORE_THREADS_H

#include <elg/move_only_function.h>

#include <cstddef>
#include <memory>

namespace elg::detail {

// A thread that starts with a stack of `stack_size` bytes, where the system's default for a thread is less than the
// thread needs: on Mikros it is 128 KiB, and a GPU driver that makes a device uses more. Like std::thread, it is
// joined before it is destroyed, or the program ends, and so does the program if `body` ends with an exception.
class StackThread {
public:
    StackThread() noexcept;
    // Starts `body`; throws std::system_error if the thread can't start.
    StackThread(std::size_t stack_size, MoveOnlyFunction<void()> body);
    ~StackThread();
    StackThread(const StackThread&) = delete;
    StackThread& operator=(const StackThread&) = delete;
    StackThread(StackThread&& other) noexcept;
    StackThread& operator=(StackThread&& other) noexcept;

    [[nodiscard]] bool joinable() const noexcept { return state_ != nullptr; }
    // Waits for the thread to end.
    void join();

private:
    // What the thread keeps (core/threads.cpp).
    struct State;
    static void run(void* state);

    std::unique_ptr<State> state_;
};

// The first thing a thread that Elegance starts makes, for as long as it runs: it sets the thread
// up for the system services Elegance's threads use, and undoes that as the thread ends (Windows:
// COM, in the multithreaded apartment, ELGSPEC-0019.R3). Only on such a thread: on any other, the
// app decides.
class ThreadSetup {
public:
    ThreadSetup() noexcept;
    ~ThreadSetup();
    ThreadSetup(const ThreadSetup&) = delete;
    ThreadSetup& operator=(const ThreadSetup&) = delete;
    ThreadSetup(ThreadSetup&&) = delete;
    ThreadSetup& operator=(ThreadSetup&&) = delete;

private:
    bool begun_;
};

} // namespace elg::detail

#endif // ELG_CORE_THREADS_H
