// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include "render/render_thread.h"

#include "core/crash.h"
#include "core/log.h"
#include "core/threads.h"
#include "gpu/atlas_texture.h"
#include "gpu/blur_renderer.h"
#include "gpu/device.h"
#include "gpu/instance.h"
#include "gpu/path_renderer.h"
#include "gpu/quad_renderer.h"
#include "gpu/texture_cache.h"
#include "gpu/webgpu.h"
#include "media/image_decode.h"
#include "media/video_source.h"
#include "render/frame_sources.h"
#include "render/gpu_image.h"
#include "render/video_textures.h"
#include "render/window_target.h"
#include "text/atlas.h"

#include <elg/error.h>

#include <webgpu/webgpu_cpp.h>

#include <format>
#include <array>
#include <memory>
#include <new>
#include <optional>
#include <unordered_map>
#include <vector>

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

// What the UI thread sent that a new device starts from: the windows, the glyphs of the atlas's
// layout, each window's last frame, and the frame sources the windows show.
struct RenderThread::Kept {
    std::unordered_map<std::uint32_t, AddWindowCommand> windows;
    std::uint64_t atlas_layout = 0;
    std::vector<GlyphUpload> atlas; // that layout's glyphs, in upload order
    std::unordered_map<std::uint32_t, WindowFrame> last_frames;
    FrameSources sources;
    std::optional<std::uint64_t> unfinished; // the batch a lost device didn't finish
};

void RenderThread::run(std::promise<void> ready) noexcept {
    // Presenting and video frames use COM on Windows (DXGI, DirectComposition, Media Foundation).
    const ThreadSetup thread_setup;
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
        // Decoders that make frames in GPU memory hand them to this renderer's device, once it
        // exists and while it runs.
        set_native_frame_importer({&imports_native_frames, &import_native_frame, &imported_frame_current});
        const gpu::Instance instance;
        Kept kept;
        // Devices lost in a row before they drew anything: a GPU that loses every new device
        // can't draw, as if none could be made.
        constexpr int kMaxFruitlessDevices = 3;
        int fruitless = 0;
        for (;;) {
            const int drawn = frames_drawn_.load();
            try {
                draw(instance, kept, ready, started);
                break; // the mailbox closed
            } catch (const gpu::DeviceLost& lost) {
                if (!started) {
                    throw; // the first device: the app doesn't start
                }
                fruitless = frames_drawn_.load() == drawn ? fruitless + 1 : 0;
                if (fruitless >= kMaxFruitlessDevices) {
                    throw GpuError(std::format("{}: {} devices in a row were lost before they drew anything",
                                               lost.what(), kMaxFruitlessDevices));
                }
                // A new device, or a GpuError when none can be made.
                log_warning("{}: drawing with a new device", lost.what());
            }
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

void RenderThread::draw(const gpu::Instance& instance, Kept& kept, std::promise<void>& ready, bool& started) {
    // Declaration order is destruction order in reverse: windows, then pipelines and the atlas,
    // then the device.
    const gpu::Device device(instance);
    devices_made_.fetch_add(1);
    set_max_texture_side(device.max_texture_side()); // images decode to fit this device's textures
    const wgpu::BindGroupLayout image_layout = gpu::create_image_layout(device.handle());
    gpu::AtlasTexture atlas(device, image_layout, kAtlasSize);
    for (const GlyphUpload& glyphs : kept.atlas) {
        atlas.upload(glyphs.x, glyphs.y, glyphs.width, glyphs.height, glyphs.rgba);
    }
    gpu::QuadRenderer quads(device, image_layout);
    gpu::BlurRenderer blur(device, image_layout);
    gpu::PathRenderer paths(device);
    gpu::TextureCache textures(device, image_layout); // outlives the windows drawing with it
    VideoTextures videos(device, image_layout);
    const RenderResources resources{quads, textures, videos, blur, paths, image_layout, atlas.group()};
    // Video decoders share frames with this device: they must run on the same GPU.
    if (const auto luid = device.luid()) {
        set_render_adapter({*luid, device.has_feature(wgpu::FeatureName::SharedTextureMemoryDXGISharedHandle)});
    }
    std::unordered_map<std::uint32_t, std::unique_ptr<WindowTarget>> windows;
    const auto add_window = [&](const AddWindowCommand& add) {
        windows.insert_or_assign(add.window_id, std::make_unique<WindowTarget>(device, add.surface, add.transparent,
                                                                               add.native_window));
    };
    for (const auto& entry : kept.windows) {
        add_window(entry.second);
    }
    const auto render = [&](const WindowFrame& frame) {
        const auto it = windows.find(frame.window_id);
        if (it == windows.end()) {
            return false;
        }
        it->second->render(frame, resources);
        frames_drawn_.fetch_add(1);
        return true;
    };

    if (!started) {
        ready.set_value();
        started = true;
        // Sources with new frames wake the render thread (when a window shows them).
        set_frame_waker([this] { mailbox_.wake(); });
    } else {
        // A new device: every window again, from its last frame. GPU images the app published
        // on the device before are another device's: they draw nothing until published again.
        for (const auto& entry : kept.last_frames) {
            render(entry.second);
        }
        if (kept.unfinished) {
            mailbox_.mark_done(*kept.unfinished);
            kept.unfinished.reset();
        }
    }

    RenderBatch batch;
    // While videos that don't wake the render thread play (the Windows and macOS players and
    // cameras, until they do), look for their next frame every few milliseconds; otherwise
    // sleep until the UI thread posts work or a source wakes it.
    while (kept.sources.polling() ? mailbox_.take_for(batch, std::chrono::milliseconds(4)) : mailbox_.take(batch)) {
        kept.unfinished = batch.sequence;
        for (RenderCommand& command : batch.commands) {
            std::visit(Overloaded{
                           [&](const AddWindowCommand& add) {
                               kept.windows.insert_or_assign(add.window_id, add);
                               add_window(add);
                           },
                           [&](const RemoveWindowCommand& remove) {
                               windows.erase(remove.window_id);
                               kept.windows.erase(remove.window_id);
                               kept.last_frames.erase(remove.window_id);
                               kept.sources.remove(remove.window_id);
                           },
                           [&](AtlasUploadCommand& upload) {
                               if (upload.layout != kept.atlas_layout) {
                                   kept.atlas.clear(); // the glyphs of the layout before
                                   kept.atlas_layout = upload.layout;
                               }
                               for (GlyphUpload& glyphs : upload.uploads) {
                                   atlas.upload(glyphs.x, glyphs.y, glyphs.width, glyphs.height, glyphs.rgba);
                                   kept.atlas.push_back(std::move(glyphs));
                               }
                           },
                           [&](const WindowVisibilityCommand& visibility) {
                               kept.sources.set_visible(visibility.window_id, visibility.visible);
                           },
                       },
                       command);
        }
        // New frames of the sources shown, taken once for all windows; then the new frames from
        // the UI thread, and every other window showing a source that has a new frame.
        kept.sources.update(batch.frames);
        for (WindowFrame& frame : batch.frames) {
            // Frames for windows removed in the meantime are dropped.
            if (!windows.contains(frame.window_id)) {
                continue;
            }
            const std::uint32_t id = frame.window_id;
            const WindowFrame& last = kept.last_frames.insert_or_assign(id, std::move(frame)).first->second;
            render(last);
            kept.sources.drew(last);
        }
        for (const std::uint32_t id : kept.sources.stale()) {
            if (render(*kept.sources.frame(id))) {
                kept.sources.redrew(id);
            }
        }
        mailbox_.mark_done(batch.sequence);
        kept.unfinished.reset();
    }
}

} // namespace elg::detail
