// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_MEDIA_VIDEO_SOURCE_H
#define ELG_MEDIA_VIDEO_SOURCE_H

#include <elg/geometry.h>
#include <elg/reactive.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace elg::detail {

// The GPU adapter the renderer uses (Windows: its LUID), published by the render thread once its
// device exists. Video decoders must use the same adapter to share frames with it.
void set_render_adapter(std::array<std::uint8_t, 8> luid);
std::optional<std::array<std::uint8_t, 8>> render_adapter();

// A frame for the renderer (design.md §13.3). How it gets to the GPU depends on the platform:
// - `d3d11_shared`: a BGRA texture the decoder shares (Windows: an NT handle), sampled in place.
//   `texture_id` changes when the texture does.
// - `cpu`: pixels in memory, in `format`, packed (no row padding), top to bottom. The renderer
//   uploads them and converts YUV to RGB in a Vulkan pass, the same on every platform. Any
//   decoder or camera can start here.
// Later: macOS IOSurfaces and Linux DMA-BUFs, imported like the D3D11 textures.
struct VideoFrame {
    enum class Kind { d3d11_shared, cpu };
    enum class Format : std::uint32_t { bgra = 0, nv12 = 1, yuy2 = 2, i420 = 3 };
    enum class Matrix : std::uint32_t { bt601 = 0, bt709 = 1 };

    Kind kind = Kind::cpu;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    // d3d11_shared
    void* shared_handle = nullptr;
    std::uint64_t texture_id = 0;
    // cpu
    Format format = Format::bgra;
    Matrix matrix = Matrix::bt709;
    bool full_range = false; // YUV: 0...255 rather than 16...235
    std::shared_ptr<const std::vector<std::uint8_t>> pixels;
    std::uint64_t source_id = 0; // the renderer keeps a ring of images per source
    std::uint64_t frame_id = 0;  // changes with every new frame
};

// Bytes in a packed frame of `format`.
std::size_t packed_size(VideoFrame::Format format, std::uint32_t width, std::uint32_t height);

// A video being played (design.md §13.3), shared by VideoPlayer copies, the views showing it, and
// frames on the render thread. The platform's media engine decodes it, plays its sound, and keeps
// them in sync.
class VideoSource : public std::enable_shared_from_this<VideoSource> {
public:
    // UI thread. `url` is a file path or an http(s) URL.
    static std::shared_ptr<VideoSource> create(std::string url);
    virtual ~VideoSource() = default;

    // --- UI thread ---
    virtual void play() = 0;
    virtual void pause() = 0;
    virtual void seek(double seconds) = 0;
    virtual void set_volume(float volume) = 0;
    virtual void set_muted(bool muted) = 0;
    virtual void set_looping(bool looping) = 0;

    // What views read; updated on the UI thread from the engine's events.
    State<bool> playing{false};
    State<bool> ready{false}; // metadata loaded: duration and size known
    State<double> time{0.0};
    State<double> duration{0.0};
    State<Size> size{Size{}}; // pixels
    State<float> volume{1.f};
    State<bool> muted{false};
    State<bool> looping{false};
    State<std::string> error{std::string()};

    // --- Render thread ---
    // Takes a new frame from the decoder if one is due; true if the current frame changed.
    virtual bool update() = 0;
    virtual std::optional<VideoFrame> current() const = 0;
    // Whether frames keep coming (the renderer redraws windows showing it).
    bool is_playing_now() const noexcept { return playing_now_.load(std::memory_order_relaxed); }

    const std::uint64_t id;

protected:
    VideoSource();
    std::atomic<bool> playing_now_{false};
};

} // namespace elg::detail

#endif // ELG_MEDIA_VIDEO_SOURCE_H
