// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_GPU_EVICTION_H
#define ELG_GPU_EVICTION_H

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <utility>
#include <vector>

namespace elg::detail::gpu {

// The clock GPU caches measure idle time with. Render thread only, like the caches.
using CacheClock = std::chrono::steady_clock;

// When a GPU cache releases what frames don't use: entries unused for `idle_limit`, and the least
// recently used ones while those the frame being drawn doesn't use take more than `spare_bytes`.
struct CachePolicy {
    CacheClock::duration idle_limit{};
    std::size_t spare_bytes = 0;
};

// An entry's last use (time and frame) and the GPU memory it takes.
struct CacheUse {
    CacheClock::time_point last_used{};
    std::uint64_t frame = 0; // CacheFrame::number() then
    std::size_t bytes = 0;
};

// The frame a cache is drawing for: begin() it once per frame, then mark() what the frame uses.
class CacheFrame {
public:
    void begin(CacheClock::time_point now) noexcept {
        now_ = now;
        ++number_;
    }
    void mark(CacheUse& use) const noexcept {
        use.last_used = now_;
        use.frame = number_;
    }
    [[nodiscard]] bool uses(const CacheUse& use) const noexcept { return use.frame == number_; }
    [[nodiscard]] CacheClock::time_point now() const noexcept { return now_; }
    [[nodiscard]] std::uint64_t number() const noexcept { return number_; }

private:
    CacheClock::time_point now_{};
    std::uint64_t number_ = 0;
};

// Releases what `policy` says to, after `frame`'s uses were marked, from one or more maps whose
// values have a CacheUse `use`: collect() each map, then finish(). The spare bytes are those of
// all the maps together, and the least recently used entry of any of them goes first.
class Eviction {
public:
    Eviction(const CacheFrame& frame, const CachePolicy& policy) : frame_(frame), policy_(policy) {}

    // Erases the entries of `entries` unused for the idle limit, and notes the others the frame
    // doesn't use. Entries for which `pinned(value)` is true are neither erased nor counted.
    template <class Map, class Pinned>
    void collect(Map& entries, const Pinned& pinned) {
        for (auto it = entries.begin(); it != entries.end();) {
            const CacheUse& use = it->second.use;
            if (frame_.uses(use) || pinned(it->second)) {
                ++it;
            } else if (frame_.now() - use.last_used >= policy_.idle_limit) {
                it = entries.erase(it);
            } else {
                spare_ += use.bytes;
                // Erasing other entries leaves `it` valid.
                unused_.push_back({use.last_used, use.bytes, [&entries, it] { entries.erase(it); }});
                ++it;
            }
        }
    }
    template <class Map>
    void collect(Map& entries) {
        collect(entries, [](const auto&) { return false; });
    }

    // Erases the least recently used entries noted until the rest take at most the policy's
    // spare bytes. Returns the bytes they take.
    std::size_t finish() {
        while (spare_ > policy_.spare_bytes && !unused_.empty()) {
            const auto oldest = std::ranges::min_element(unused_, {}, &Unused::last_used);
            spare_ -= oldest->bytes;
            oldest->erase();
            if (oldest != std::prev(unused_.end())) {
                *oldest = std::move(unused_.back());
            }
            unused_.pop_back();
        }
        return spare_;
    }

private:
    struct Unused {
        CacheClock::time_point last_used;
        std::size_t bytes = 0;
        std::function<void()> erase;
    };
    const CacheFrame& frame_;
    CachePolicy policy_;
    std::size_t spare_ = 0;
    std::vector<Unused> unused_;
};

// Eviction of one map. Returns the bytes its remaining entries that `frame` doesn't use take.
template <class Map, class Pinned>
std::size_t evict(Map& entries, const CacheFrame& frame, const CachePolicy& policy, const Pinned& pinned) {
    Eviction eviction(frame, policy);
    eviction.collect(entries, pinned);
    return eviction.finish();
}

template <class Map>
std::size_t evict(Map& entries, const CacheFrame& frame, const CachePolicy& policy) {
    return evict(entries, frame, policy, [](const auto&) { return false; });
}

} // namespace elg::detail::gpu

#endif // ELG_GPU_EVICTION_H
