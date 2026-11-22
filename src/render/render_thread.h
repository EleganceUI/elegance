// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_RENDER_RENDER_THREAD_H
#define ELG_RENDER_RENDER_THREAD_H

#include "render/frame_mailbox.h"

#include <elg/move_only_function.h>

#include <atomic>
#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <thread>

namespace elg::detail {

namespace gpu {
class Instance;
class PipelineCache;
}

// Owns the WebGPU instance, the device, and every GPU resource, window surfaces included;
// consumes work from the FrameMailbox (design.md §15). The UI thread never touches GPU objects.
// When the device is lost, it starts over on a new one, which draws every window again
// (ELGSPEC-0025.R4); only when no new device can be made does an error stop it. It makes the pipeline cache,
// which every device it makes shares, and which reads and writes files; the UI thread, which waits for the
// first device, never does (ELGSPEC-0036.R9).
class RenderThread {
public:
    // Starts the thread and blocks until the GPU is initialized; rethrows initialization
    // errors. `on_error` is called from the render thread after a developer-facing error stopped
    // it.
    RenderThread(FrameMailbox& mailbox, MoveOnlyFunction<void()> on_error);
    ~RenderThread();

    RenderThread(const RenderThread&) = delete;
    RenderThread& operator=(const RenderThread&) = delete;
    RenderThread(RenderThread&&) = delete;
    RenderThread& operator=(RenderThread&&) = delete;

    // The elg::Error (or std::bad_alloc) that stopped the thread, if any.
    std::exception_ptr error() const;

    // For tests: how many devices it made (1, and one more per lost device), and how many frames
    // of windows it drew.
    [[nodiscard]] int devices_made() const noexcept { return devices_made_.load(); }
    [[nodiscard]] int frames_drawn() const noexcept { return frames_drawn_.load(); }

private:
    // What a new device starts from: what the UI thread sent that outlives a device.
    struct Kept;

    void run(std::promise<void> ready) noexcept;
    // Draws with a device of its own until the mailbox closes; throws gpu::DeviceLost when the
    // device is lost. The first device signals `ready` once it can draw (`started`).
    void draw(const gpu::Instance& instance, const std::shared_ptr<gpu::PipelineCache>& cache, Kept& kept,
              std::promise<void>& ready, bool& started);

    FrameMailbox& mailbox_;
    MoveOnlyFunction<void()> on_error_;
    mutable std::mutex error_mutex_;
    std::exception_ptr error_;
    std::atomic<int> devices_made_{0};
    std::atomic<int> frames_drawn_{0};
    std::thread thread_;
};

} // namespace elg::detail

#endif // ELG_RENDER_RENDER_THREAD_H
