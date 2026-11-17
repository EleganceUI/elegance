// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include "media/frame_source.h"

#include <atomic>
#include <mutex>
#include <utility>

namespace elg::detail {

namespace {

std::atomic<std::uint64_t> g_next_id{1};
// Called with the mutex held, so that it can't run once cleared. Recursive: what a waker does may
// wake it again.
std::recursive_mutex g_waker_mutex;
std::function<void()> g_waker;

} // namespace

std::uint64_t next_media_id() {
    return g_next_id.fetch_add(1);
}

void set_frame_waker(std::function<void()> waker) {
    const std::lock_guard lock(g_waker_mutex);
    g_waker = std::move(waker);
}

FrameSource::FrameSource(bool wakes_for_frames) : id(next_media_id()), wakes_for_frames_(wakes_for_frames) {}

void FrameSource::notify_new_frame() const {
    if (!shown_) {
        return; // the render thread updates it once a window shows it
    }
    const std::lock_guard lock(g_waker_mutex);
    if (g_waker) {
        g_waker();
    }
}

} // namespace elg::detail
