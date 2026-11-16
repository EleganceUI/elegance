// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_MEDIA_VIDEO_SOURCE_H
#define ELG_MEDIA_VIDEO_SOURCE_H

#include "media/video_frame.h"

#include <elg/geometry.h>
#include <elg/reactive.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace elg::detail {

// Published by the render thread once its device exists (RenderAdapter, media/video_frame.h).
void set_render_adapter(RenderAdapter adapter);
std::optional<RenderAdapter> render_adapter();

// A new id, unique among sources and the textures decoders share (VideoFrame::texture_id).
std::uint64_t next_media_id();

// What VideoSource::notify_new_frame() calls to wake the render thread: set by the render thread
// while it runs, and cleared (an empty function) before it stops. Any thread.
void set_frame_waker(std::function<void()> waker);

// A video being played (design.md §13.3), shared by VideoPlayer copies, the views showing it, and
// frames on the render thread. The platform's media engine decodes it, plays its sound, and keeps
// them in sync.
class VideoSource : public std::enable_shared_from_this<VideoSource> {
public:
    // UI thread. `url` is a file path or an http(s) URL.
    static std::shared_ptr<VideoSource> create(const std::string& url);
    // A source that never plays, with `message` as its error.
    static std::shared_ptr<VideoSource> unsupported(std::string message);
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
    // Whether frames keep coming.
    bool is_playing_now() const noexcept { return playing_now_.load(std::memory_order_relaxed); }
    // Whether it calls notify_new_frame() for each new frame; otherwise the render thread looks
    // for frames regularly while it plays.
    [[nodiscard]] bool wakes_for_frames() const noexcept { return wakes_for_frames_; }
    // Whether a window shows it (FrameSources): only then do its new frames wake the render
    // thread.
    void set_shown(bool shown) noexcept { shown_ = shown; }

    const std::uint64_t id;

protected:
    // `wakes_for_frames`: see wakes_for_frames().
    explicit VideoSource(bool wakes_for_frames = false);
    // Any thread, after the source's state changed: wakes the render thread, when a window shows
    // it, to draw its new frame (or the lack of one).
    void notify_new_frame() const;

    std::atomic<bool> playing_now_{false};

private:
    const bool wakes_for_frames_;
    std::atomic<bool> shown_{false};
};

// VideoSource::create's backend: each OS's media engine (platform/<os>/video_source.*). UI thread.
std::shared_ptr<VideoSource> create_platform_video_source(const std::string& url);

} // namespace elg::detail

#endif // ELG_MEDIA_VIDEO_SOURCE_H
