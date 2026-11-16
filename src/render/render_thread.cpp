// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include "render/render_thread.h"

#include "core/crash.h"
#include "gpu/device.h"
#include "gpu/instance.h"
#include "gpu/atlas_texture.h"
#include "gpu/blur_renderer.h"
#include "gpu/quad_renderer.h"
#include "render/frame_sources.h"
#include "render/window_target.h"

#include <elg/error.h>

#include <format>
#include <array>
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

RenderThread::RenderThread(FrameMailbox& mailbox, MoveOnlyFunction<void()> on_error)
    : mailbox_(mailbox), on_error_(std::move(on_error)) {
    std::promise<void> ready;
    std::future<void> initialized = ready.get_future();
    thread_ = std::thread(&RenderThread::run, this, std::move(ready));
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
    const std::lock_guard lock(error_mutex_);
    return error_;
}

void RenderThread::run(std::promise<void> ready) noexcept {
    bool started = false;
    const auto fail = [&](const std::exception_ptr& error) {
        if (!started) {
            ready.set_exception(error);
            return;
        }
        {
            const std::lock_guard lock(error_mutex_);
            error_ = error;
        }
        on_error_();
    };

    try {
        // Declaration order is destruction order in reverse: windows, then pipelines and the
        // atlas, then the device and the instance.
        const gpu::Instance instance;
        const gpu::Device device(instance);
        const wgpu::BindGroupLayout image_layout = gpu::create_image_layout(device.handle());
        gpu::AtlasTexture atlas(device, image_layout);
        gpu::QuadRenderer quads(device, image_layout);
        gpu::BlurRenderer blur(device, image_layout);
        gpu::PathRenderer paths(device);
        gpu::TextureCache textures(device, image_layout); // outlives the windows drawing with it
        gpu::VideoTextures videos(device, image_layout);
        const RenderResources resources{quads, textures, videos, blur, paths, image_layout, atlas.group()};
        // Video decoders share frames with this device: they must run on the same GPU.
        if (const auto luid = device.luid()) {
            set_render_adapter({*luid, device.has_feature(wgpu::FeatureName::SharedTextureMemoryDXGISharedHandle)});
        }
        // The last frame of each window showing a frame source: drawn again when one of them has
        // a new frame.
        FrameSources sources;
        std::unordered_map<std::uint32_t, std::unique_ptr<WindowTarget>> windows;

        ready.set_value();
        started = true;
        // Sources with new frames wake the render thread (when a window shows them).
        set_frame_waker([this] { mailbox_.wake(); });

        RenderBatch batch;
        // While videos that don't wake the render thread play (the Windows and macOS players and
        // cameras, until they do), look for their next frame every few milliseconds; otherwise
        // sleep until the UI thread posts work or a source wakes it.
        while (sources.polling() ? mailbox_.take_for(batch, std::chrono::milliseconds(4)) : mailbox_.take(batch)) {
            for (RenderCommand& command : batch.commands) {
                std::visit(Overloaded{
                               [&](const AddWindowCommand& add) {
                                   windows.emplace(add.window_id,
                                                   std::make_unique<WindowTarget>(
                                                       device, instance.create_surface(add.surface),
                                                       add.transparent, add.native_window));
                               },
                               [&](const RemoveWindowCommand& remove) {
                                   windows.erase(remove.window_id);
                                   sources.remove(remove.window_id);
                               },
                               [&](const AtlasUploadCommand& upload) { atlas.upload(upload.uploads); },
                               [&](const WindowVisibilityCommand& visibility) {
                                   sources.set_visible(visibility.window_id, visibility.visible);
                               },
                           },
                           command);
            }
            // New frames of the sources shown, taken once for all windows; then the new frames
            // from the UI thread, and every other window showing a source that has a new frame.
            sources.update(batch.frames);
            for (const WindowFrame& frame : batch.frames) {
                // Frames for windows removed in the meantime are dropped.
                if (auto it = windows.find(frame.window_id); it != windows.end()) {
                    it->second->render(frame, resources);
                    sources.drew(frame);
                }
            }
            for (const std::uint32_t id : sources.stale()) {
                if (auto it = windows.find(id); it != windows.end()) {
                    it->second->render(*sources.frame(id), resources);
                    sources.redrew(id);
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

    set_frame_waker({});
    mailbox_.close();
}

} // namespace elg::detail
