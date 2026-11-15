// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_MEDIA_SAMPLE_RING_H
#define ELG_MEDIA_SAMPLE_RING_H

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace elg::detail {

// Audio samples between two threads, without locks, so an audio device's real-time thread can be
// either side: one writer and one reader at a time. When the writer runs out of room it drops the
// oldest samples; a reader copying those meanwhile gets some of the newer ones instead (a glitch,
// as dropping is anyway). Writes and reads are whole frames when the capacity and every write
// and read size are.
class SampleRing {
public:
    explicit SampleRing(std::size_t capacity)
        : slots_(std::make_unique<std::atomic<float>[]>(std::max<std::size_t>(capacity, 1))),
          capacity_(std::max<std::size_t>(capacity, 1)) {}

    // Writer: appends `samples`, keeping the newest `capacity` of all those written.
    void write(std::span<const float> samples) noexcept {
        if (samples.size() > capacity_) {
            samples = samples.last(capacity_);
        }
        const std::uint64_t start = written_.load(std::memory_order_relaxed);
        const std::uint64_t end = start + samples.size();
        std::uint64_t oldest = read_.load(std::memory_order_acquire);
        while (end - oldest > capacity_ &&
               !read_.compare_exchange_weak(oldest, end - capacity_, std::memory_order_acq_rel)) {
        }
        for (std::size_t i = 0; i < samples.size(); ++i) {
            slots_[(start + i) % capacity_].store(samples[i], std::memory_order_relaxed);
        }
        written_.store(end, std::memory_order_release);
    }

    // Reader: takes up to out.size() of the oldest samples into `out`; returns how many.
    std::size_t read(std::span<float> out) noexcept {
        std::uint64_t start = read_.load(std::memory_order_acquire);
        const std::uint64_t end = written_.load(std::memory_order_acquire);
        const std::size_t count = std::min({out.size(), capacity_, static_cast<std::size_t>(end - std::min(start, end))});
        for (std::size_t i = 0; i < count; ++i) {
            out[i] = slots_[(start + i) % capacity_].load(std::memory_order_relaxed);
        }
        // Unless the writer dropped them meanwhile, and so moved past them already.
        read_.compare_exchange_strong(start, start + count, std::memory_order_acq_rel);
        return count;
    }

    // Writer: drops everything written so far.
    void clear() noexcept { read_.store(written_.load(std::memory_order_relaxed), std::memory_order_release); }

    // How many samples are waiting.
    [[nodiscard]] std::size_t size() const noexcept {
        const std::uint64_t start = read_.load(std::memory_order_acquire);
        const std::uint64_t end = written_.load(std::memory_order_acquire);
        return static_cast<std::size_t>(std::min<std::uint64_t>(end - std::min(start, end), capacity_));
    }

private:
    std::unique_ptr<std::atomic<float>[]> slots_;
    std::size_t capacity_;
    std::atomic<std::uint64_t> read_{0};    // the oldest sample waiting
    std::atomic<std::uint64_t> written_{0}; // one past the newest
};

} // namespace elg::detail

#endif // ELG_MEDIA_SAMPLE_RING_H
