// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_GPU_EVICTION_H
#define ELG_GPU_EVICTION_H

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
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

// Erases from `entries` (a map whose values have a CacheUse `use`) what `policy` releases after
// `frame`'s uses were marked. Entries for which `pinned(value)` is true are neither erased nor
// counted. Returns the bytes the remaining entries that `frame` doesn't use take.
template <class Map, class Pinned>
std::size_t evict(Map& entries, const CacheFrame& frame, const CachePolicy& policy, const Pinned& pinned) {
    std::size_t spare = 0;
    std::vector<typename Map::iterator> unused; // erasing others leaves these valid
    for (auto it = entries.begin(); it != entries.end();) {
        const CacheUse& use = it->second.use;
        if (frame.uses(use) || pinned(it->second)) {
            ++it;
        } else if (frame.now() - use.last_used >= policy.idle_limit) {
            it = entries.erase(it);
        } else {
            spare += use.bytes;
            unused.push_back(it);
            ++it;
        }
    }
    while (spare > policy.spare_bytes && !unused.empty()) {
        const auto oldest =
            std::ranges::min_element(unused, {}, [](const auto& it) { return it->second.use.last_used; });
        spare -= (*oldest)->second.use.bytes;
        entries.erase(*oldest);
        *oldest = unused.back();
        unused.pop_back();
    }
    return spare;
}

template <class Map>
std::size_t evict(Map& entries, const CacheFrame& frame, const CachePolicy& policy) {
    return evict(entries, frame, policy, [](const auto&) { return false; });
}

} // namespace elg::detail::gpu

#endif // ELG_GPU_EVICTION_H
