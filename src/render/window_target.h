// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_RENDER_WINDOW_TARGET_H
#define ELG_RENDER_WINDOW_TARGET_H

#include "gpu/surface_source.h"
#include "gpu/webgpu_types.h"
#include "render/frame_mailbox.h"

#include <memory>
#include <span>

namespace elg::detail {

namespace gpu {
class BlurRenderer;
class Device;
class PathRenderer;
class Presenter;
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
    // Draws into `presenter` (tests draw into offscreen images).
    WindowTarget(const gpu::Device& device, std::unique_ptr<gpu::Presenter> presenter);
    ~WindowTarget();

    WindowTarget(const WindowTarget&) = delete;
    WindowTarget& operator=(const WindowTarget&) = delete;
    WindowTarget(WindowTarget&&) = delete;
    WindowTarget& operator=(WindowTarget&&) = delete;

    // Renders and presents the frame. Does nothing while the window is minimized.
    void render(const WindowFrame& frame, const RenderResources& resources);
    // Tells it that these glyphs' pixels were put in the atlas. The glyphs of its frames that show them are drawn again
    // in the next frame: a glyph's quad is in the frames from the one that asks for it, and its pixels may reach the
    // atlas after the frame that was drawn without them, which has nothing else to tell the two frames apart by.
    void atlas_uploaded(std::span<const GlyphUpload> uploads);
    // How many frames it presented: frames without damage aren't (ELGSPEC-0029.R7).
    [[nodiscard]] int frames_presented() const noexcept;
    // How many tiles of scroll views it drew: kept tiles aren't drawn again (ELGSPEC-0029.R13).
    [[nodiscard]] int tiles_drawn() const noexcept;
    // The bytes its intermediate images take.
    [[nodiscard]] std::size_t intermediate_bytes() const noexcept;

private:
    // What it keeps across frames, and the recording of a frame with it.
    class State;
    std::unique_ptr<State> state_;
};

} // namespace elg::detail

#endif // ELG_RENDER_WINDOW_TARGET_H
