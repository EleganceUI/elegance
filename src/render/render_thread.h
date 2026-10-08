// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_RENDER_RENDER_THREAD_H
#define ELG_RENDER_RENDER_THREAD_H

#include "gpu/instance.h"
#include "render/frame_mailbox.h"

#include <exception>
#include <functional>
#include <future>
#include <mutex>
#include <thread>

namespace elg::detail {

// Owns the GPU device and every GPU resource; consumes work from the FrameMailbox
// (design.md §15). The UI thread never touches Vulkan objects except for creating and
// destroying window surfaces, which SDL requires to happen on the main thread.
class RenderThread {
public:
    // Starts the thread and blocks until the GPU is initialized; rethrows initialization
    // errors. `compatibility_surface` picks a GPU that can present to the app's windows.
    // `on_error` is called from the render thread after a developer-facing error stopped it.
    RenderThread(const gpu::Instance& instance, VkSurfaceKHR compatibility_surface,
                 FrameMailbox& mailbox, std::move_only_function<void()> on_error);
    ~RenderThread();

    RenderThread(const RenderThread&) = delete;
    RenderThread& operator=(const RenderThread&) = delete;

    // The elg::Error (or std::bad_alloc) that stopped the thread, if any.
    std::exception_ptr error() const;

private:
    void run(VkSurfaceKHR compatibility_surface, std::promise<void> ready) noexcept;

    const gpu::Instance& instance_;
    FrameMailbox& mailbox_;
    std::move_only_function<void()> on_error_;
    mutable std::mutex error_mutex_;
    std::exception_ptr error_;
    std::thread thread_;
};

} // namespace elg::detail

#endif // ELG_RENDER_RENDER_THREAD_H
