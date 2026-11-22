// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// The thread that starts with a stack of its own size (core/threads.h): the render thread's, which the GPU's drivers
// need more of than a system's default.

#include "core/threads.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cstddef>
#include <thread>
#include <utility>

using elg::detail::StackThread;

TEST_CASE("a stack thread runs its body on another thread, and ends when it is joined", "[threads]") {
    std::atomic<bool> ran{false};
    std::thread::id where;
    StackThread thread(std::size_t{1} << 20, [&] {
        where = std::this_thread::get_id();
        ran = true;
    });
    CHECK(thread.joinable());
    thread.join();
    CHECK_FALSE(thread.joinable());
    CHECK(ran);
    CHECK(where != std::this_thread::get_id());
}

TEST_CASE("a stack thread can be moved, and the one that was moved from has nothing to join", "[threads]") {
    std::atomic<int> runs{0};
    StackThread first(std::size_t{1} << 20, [&] { ++runs; });
    StackThread second = std::move(first);
    CHECK_FALSE(first.joinable()); // NOLINT(bugprone-use-after-move): that is what is checked
    CHECK(second.joinable());
    second.join();
    CHECK(runs == 1);
    StackThread none;
    CHECK_FALSE(none.joinable());
    none.join();
}

#if !defined(_WIN32)
namespace {

// Uses `depth` blocks of 64 KiB of the stack, and writes to every page of each, so that a stack that is too small
// has its guard page touched and not jumped over; it returns the number of blocks it used. The empty statement of
// assembly that is given the block makes the compiler keep it in the stack, and not turn the calls into a loop.
std::size_t use_stack(std::size_t depth) {
    volatile unsigned char block[std::size_t{64} << 10];
    for (std::size_t at = 0; at < sizeof block; at += 4096) {
        block[at] = 1;
    }
    asm volatile("" : : "r"(&block[0]) : "memory");
    return depth == 0 ? std::size_t{block[0]} : use_stack(depth - 1) + std::size_t{block[0]};
}

} // namespace

TEST_CASE("a stack thread has the stack that it was asked for, which can be more than the system's default",
          "[threads]") {
    // 32 MiB in use: more than the 8 MiB that a thread has on Linux, and than the 512 KiB on macOS.
    constexpr std::size_t stack = std::size_t{64} << 20;
    constexpr std::size_t depth = (std::size_t{32} << 20) / (std::size_t{64} << 10);
    std::atomic<std::size_t> used{0};
    StackThread thread(stack, [&] { used = use_stack(depth); });
    thread.join();
    CHECK(used == depth + 1);
}
#endif
