// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_RENDER_RENDER_THREAD_H
#define ELG_RENDER_RENDER_THREAD_H

#include "render/frame_mailbox.h"

#include <elg/move_only_function.h>

#include <exception>
#include <functional>
#include <future>
#include <mutex>
#include <thread>

namespace elg::detail {

// Owns the WebGPU instance, the device, and every GPU resource, window surfaces included;
// consumes work from the FrameMailbox (design.md §15). The UI thread never touches GPU objects.
class RenderThread {
public:
    // Starts the thread and blocks until the GPU is initialized; rethrows initialization
    // errors. `on_error` is called from the render thread after a developer-facing error stopped
    // it.
    RenderThread(FrameMailbox& mailbox, MoveOnlyFunction<void()> on_error);
    ~RenderThread();

    RenderThread(const RenderThread&) = delete;
    RenderThread& operator=(const RenderThread&) = delete;

    // The elg::Error (or std::bad_alloc) that stopped the thread, if any.
    std::exception_ptr error() const;

private:
    void run(std::promise<void> ready) noexcept;

    FrameMailbox& mailbox_;
    MoveOnlyFunction<void()> on_error_;
    mutable std::mutex error_mutex_;
    std::exception_ptr error_;
    std::thread thread_;
};

} // namespace elg::detail

#endif // ELG_RENDER_RENDER_THREAD_H
