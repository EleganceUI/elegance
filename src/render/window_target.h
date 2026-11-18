// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_RENDER_WINDOW_TARGET_H
#define ELG_RENDER_WINDOW_TARGET_H

#include "gpu/surface_source.h"
#include "gpu/webgpu_types.h"
#include "render/frame_mailbox.h"

#include <memory>

namespace elg::detail {

namespace gpu {
class BlurRenderer;
class Device;
class PathRenderer;
class QuadRenderer;
class TextureCache;
} // namespace gpu

class VideoTextures;

// Render-thread objects shared by all windows.
struct RenderResources {
    gpu::QuadRenderer& quads;
    gpu::TextureCache& textures;
    VideoTextures& videos;
    gpu::BlurRenderer& blur;
    gpu::PathRenderer& paths;
    const wgpu::BindGroupLayout& image_layout; // a texture and its sampler (atlas, layers)
    const wgpu::BindGroup& atlas;
};

// Everything the render thread needs to draw into one window: its presenter (gpu::Presenter),
// the frame's buffers, and pooled offscreen images (IntermediateImages). WebGPU orders
// queue writes and submissions, so one set serves every frame, even while earlier ones are still
// on the GPU.
class WindowTarget {
public:
    // `native_window` (an HWND on Windows) lets windows present through the platform
    // compositor, which can show transparency (WebGPU surfaces there can't).
    WindowTarget(const gpu::Device& device, const gpu::SurfaceSource& surface, bool transparent = false,
                 void* native_window = nullptr);
    ~WindowTarget();

    WindowTarget(const WindowTarget&) = delete;
    WindowTarget& operator=(const WindowTarget&) = delete;
    WindowTarget(WindowTarget&&) = delete;
    WindowTarget& operator=(WindowTarget&&) = delete;

    // Renders and presents the frame. Does nothing while the window is minimized.
    void render(const WindowFrame& frame, const RenderResources& resources);

private:
    // What it keeps across frames, and the recording of a frame with it.
    class State;
    std::unique_ptr<State> state_;
};

} // namespace elg::detail

#endif // ELG_RENDER_WINDOW_TARGET_H
