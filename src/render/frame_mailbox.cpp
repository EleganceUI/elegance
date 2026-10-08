// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include "render/frame_mailbox.h"

#include <algorithm>
#include <utility>

namespace elg::detail {

std::uint64_t FrameMailbox::post(RenderCommand command) {
    std::lock_guard lock(mutex_);
    if (closed_) {
        return done_;
    }
    commands_.push_back(std::move(command));
    work_ready_.notify_one();
    return ++posted_;
}

std::uint64_t FrameMailbox::post(WindowFrame frame) {
    std::lock_guard lock(mutex_);
    if (closed_) {
        return done_;
    }
    auto pending = std::ranges::find(frames_, frame.window_id, &WindowFrame::window_id);
    if (pending != frames_.end()) {
        *pending = std::move(frame);
    } else {
        frames_.push_back(std::move(frame));
    }
    work_ready_.notify_one();
    return ++posted_;
}

bool FrameMailbox::wait_until_done(std::uint64_t sequence, std::chrono::milliseconds timeout) {
    std::unique_lock lock(mutex_);
    return work_done_.wait_for(lock, timeout, [&] { return closed_ || done_ >= sequence; });
}

void FrameMailbox::wait_until_done(std::uint64_t sequence) {
    std::unique_lock lock(mutex_);
    work_done_.wait(lock, [&] { return closed_ || done_ >= sequence; });
}

void FrameMailbox::request_stop() {
    std::lock_guard lock(mutex_);
    stop_ = true;
    work_ready_.notify_one();
}

bool FrameMailbox::take(RenderBatch& batch) {
    std::unique_lock lock(mutex_);
    work_ready_.wait(lock, [&] { return stop_ || !commands_.empty() || !frames_.empty(); });
    if (stop_) {
        return false;
    }
    batch.commands = std::exchange(commands_, {});
    batch.frames = std::exchange(frames_, {});
    batch.sequence = posted_;
    return true;
}

void FrameMailbox::mark_done(std::uint64_t sequence) {
    std::lock_guard lock(mutex_);
    done_ = std::max(done_, sequence);
    work_done_.notify_all();
}

void FrameMailbox::close() {
    std::lock_guard lock(mutex_);
    closed_ = true;
    done_ = posted_;
    commands_.clear();
    frames_.clear();
    work_done_.notify_all();
}

} // namespace elg::detail
