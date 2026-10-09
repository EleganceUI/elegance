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
#include <algorithm>
#include <cstring>
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
        gpu::VideoTextures videos(device, atlas.set_layout());
        const RenderResources resources{quads, textures, videos, blur, paths, atlas.set_layout(), atlas.descriptor_set()};
        // Video decoders share frames with this device: they must run on the same GPU.
        {
            VkPhysicalDeviceIDProperties ids{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
            VkPhysicalDeviceProperties2 properties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
            properties.pNext = &ids;
            vkGetPhysicalDeviceProperties2(device.physical(), &properties);
            if (ids.deviceLUIDValid) {
                std::array<std::uint8_t, 8> luid{};
                std::memcpy(luid.data(), ids.deviceLUID, luid.size());
                set_render_adapter(luid);
            }
        }
        // The last frame of each window showing a video: redrawn when the video has a new frame.
        std::unordered_map<std::uint32_t, WindowFrame> video_frames;
        const auto shows_video = [](const WindowFrame& frame) {
            return std::ranges::any_of(frame.list.commands, [](const DrawCommand& command) {
                const auto* video = std::get_if<VideoCommand>(&command);
                return video && video->source;
            });
        };
        std::unordered_map<std::uint32_t, std::unique_ptr<WindowTarget>> windows;

        ready.set_value();
        started = true;

        RenderBatch batch;
        const auto any_playing = [&] {
            for (const auto& [id, frame] : video_frames) {
                for (const DrawCommand& command : frame.list.commands) {
                    if (const auto* video = std::get_if<VideoCommand>(&command); video && video->source->is_playing_now()) {
                        return true;
                    }
                }
            }
            return false;
        };
        // While videos play, wake every few milliseconds to check for their next frame; otherwise
        // sleep until the UI thread posts work.
        while (any_playing() ? mailbox_.take_for(batch, std::chrono::milliseconds(4)) : mailbox_.take(batch)) {
            if (batch.commands.empty() && batch.frames.empty()) {
                for (auto& [id, frame] : video_frames) {
                    auto window = windows.find(id);
                    if (window == windows.end()) {
                        continue;
                    }
                    bool fresh = false;
                    for (const DrawCommand& command : frame.list.commands) {
                        if (const auto* video = std::get_if<VideoCommand>(&command)) {
                            fresh |= video->source->update();
                        }
                    }
                    if (fresh) {
                        window->second->render(frame, resources);
                    }
                }
                continue;
            }
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
                               [&](const RemoveWindowCommand& remove) {
                                   windows.erase(remove.window_id);
                                   video_frames.erase(remove.window_id);
                               },
                               [&](const AtlasUploadCommand& upload) { atlas.upload(upload.uploads); },
                           },
                           command);
            }
            for (const WindowFrame& frame : batch.frames) {
                // Frames for windows removed in the meantime are dropped.
                if (auto it = windows.find(frame.window_id); it != windows.end()) {
                    it->second->render(frame, resources);
                    if (shows_video(frame)) {
                        video_frames.insert_or_assign(frame.window_id, frame);
                    } else {
                        video_frames.erase(frame.window_id);
                    }
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
