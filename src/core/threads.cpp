// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include "core/threads.h"

#include "core/os_backend.h"

#include <exception>
#include <utility>

namespace elg::detail {

struct StackThread::State {
    void* handle = nullptr;
    MoveOnlyFunction<void()> body;
};

// The thread's entry: an exception that leaves the body ends the program, as it does from a std::thread.
void StackThread::run(void* state) {
    try {
        static_cast<State*>(state)->body();
    } catch (...) {
        std::terminate();
    }
}

StackThread::StackThread() noexcept = default;

StackThread::StackThread(std::size_t stack_size, MoveOnlyFunction<void()> body) : state_(std::make_unique<State>()) {
    state_->body = std::move(body);
    state_->handle = os::start_thread_with_stack(stack_size, &StackThread::run, state_.get());
}

void StackThread::join() {
    if (state_) {
        os::join_thread(state_->handle);
        state_.reset();
    }
}

StackThread::~StackThread() {
    if (state_) {
        std::terminate(); // as with std::thread: it was not joined
    }
}

StackThread::StackThread(StackThread&& other) noexcept = default;

StackThread& StackThread::operator=(StackThread&& other) noexcept {
    if (state_) {
        std::terminate();
    }
    state_ = std::move(other.state_);
    return *this;
}

ThreadSetup::ThreadSetup() noexcept : begun_(os::begin_elegance_thread()) {}

ThreadSetup::~ThreadSetup() {
    os::end_elegance_thread(begun_);
}

} // namespace elg::detail
