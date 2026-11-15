// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// How long the renderer keeps GPU memory that frames don't use, and how much of it. Times are
// given, not read from a clock.

#include "gpu/eviction.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <map>

using namespace elg;
using namespace std::chrono_literals;

namespace {

const detail::gpu::CacheClock::time_point t0{};

} // namespace

TEST_CASE("GPU caches release entries by idle time and by the bytes the frame doesn't use", "[gpu-memory]") {
    struct Entry {
        detail::gpu::CacheUse use;
        bool pinned = false;
    };
    std::map<int, Entry> entries;
    detail::gpu::CacheFrame frame;
    const detail::gpu::CachePolicy policy{10s, 100};

    frame.begin(t0);
    for (int i = 0; i < 4; ++i) {
        frame.mark(entries[i].use);
        entries[i].use.bytes = 40;
    }
    entries[3].pinned = true;
    frame.begin(t0 + 1s);
    frame.mark(entries[0].use);
    const auto pinned = [](const Entry& entry) { return entry.pinned; };
    // 1 and 2 are unused (80 bytes): within the budget.
    CHECK(detail::gpu::evict(entries, frame, policy, pinned) == 80);
    CHECK(entries.size() == 4);

    frame.begin(t0 + 2s);
    entries[4].use.bytes = 40;
    frame.mark(entries[4].use);
    frame.begin(t0 + 3s);
    // 0, 1, 2 and 4 are unused (160 bytes): 1 and 2, the least recently used, go.
    CHECK(detail::gpu::evict(entries, frame, policy, pinned) == 80);
    CHECK(entries.size() == 3);
    CHECK(entries.contains(0));
    CHECK(entries.contains(3));
    CHECK(entries.contains(4));

    // Unused for 10 s: released, unless pinned.
    frame.begin(t0 + 12s);
    CHECK(detail::gpu::evict(entries, frame, policy, pinned) == 0);
    CHECK(entries.size() == 1);
    CHECK(entries.contains(3));
}
