// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include "render/render_thread.h"

#include "core/crash.h"
#include "core/log.h"
#include "gpu/device.h"
#include "gpu/atlas_texture.h"
#include "gpu/blur_renderer.h"
#include "gpu/quad_renderer.h"
#include "render/window_target.h"

#include <elg/error.h>

#include <format>
#include <memory>
#include <new>
#include <unordered_map>

namespace elg::detail {

namespace {

template <class... Fs>
struct Overloaded : Fs... {
    using Fs::operator()...;
};

} // namespace

RenderThread::RenderThread(const gpu::Instance& instance, VkSurfaceKHR compatibility_surface,
                           FrameMailbox& mailbox, std::move_only_function<void()> on_error)
    : instance_(instance), mailbox_(mailbox), on_error_(std::move(on_error)) {
    std::promise<void> ready;
    std::future<void> initialized = ready.get_future();
    thread_ = std::thread(&RenderThread::run, this, compatibility_surface, std::move(ready));
    try {
        initialized.get();
    } catch (...) {
        thread_.join();
        throw;
    }
}

RenderThread::~RenderThread() {
    mailbox_.request_stop();
    if (thread_.joinable()) {
        thread_.join();
    }
}

std::exception_ptr RenderThread::error() const {
    std::lock_guard lock(error_mutex_);
    return error_;
}

void RenderThread::run(VkSurfaceKHR compatibility_surface, std::promise<void> ready) noexcept {
    bool started = false;
    const auto fail = [&](std::exception_ptr error) {
        if (!started) {
            ready.set_exception(error);
            return;
        }
        {
            std::lock_guard lock(error_mutex_);
            error_ = error;
        }
        on_error_();
    };

    try {
        // Declaration order is destruction order in reverse: windows, then pipelines and the
        // atlas, then the device.
        gpu::Device device(instance_, compatibility_surface);
        gpu::AtlasTexture atlas(device);
        gpu::QuadRenderer quads(device, atlas.set_layout());
        gpu::BlurRenderer blur(device, atlas.set_layout());
        gpu::PathRenderer paths(device);
        gpu::TextureCache textures(device, atlas.set_layout()); // outlives the windows drawing with it
        const RenderResources resources{quads, textures, blur, paths, atlas.set_layout(), atlas.descriptor_set()};
        std::unordered_map<std::uint32_t, std::unique_ptr<WindowTarget>> windows;

        ready.set_value();
        started = true;

        RenderBatch batch;
        while (mailbox_.take(batch)) {
            for (RenderCommand& command : batch.commands) {
                std::visit(Overloaded{
                               [&](const AddWindowCommand& add) {
                                   if (!device.can_present(add.surface)) {
                                       throw GpuError(std::format(
                                           "the GPU \"{}\" can't present to window {}",
                                           device.name(), add.window_id));
                                   }
                                   windows.emplace(add.window_id,
                                                   std::make_unique<WindowTarget>(device, add.surface, add.transparent,
                                                                                  add.native_window));
                               },
                               [&](const RemoveWindowCommand& remove) { windows.erase(remove.window_id); },
                               [&](const AtlasUploadCommand& upload) { atlas.upload(upload.uploads); },
                           },
                           command);
            }
            for (const WindowFrame& frame : batch.frames) {
                // Frames for windows removed in the meantime are dropped.
                if (auto it = windows.find(frame.window_id); it != windows.end()) {
                    it->second->render(frame, resources);
                }
            }
            mailbox_.mark_done(batch.sequence);
        }
    } catch (const Error&) {
        fail(std::current_exception());
    } catch (const std::bad_alloc&) {
        fail(std::current_exception());
    } catch (const std::exception& e) {
        internal_error(std::format("unexpected exception on the render thread: {}", e.what()));
    } catch (...) {
        internal_error("exception of unknown type on the render thread");
    }

    mailbox_.close();
}

} // namespace elg::detail
