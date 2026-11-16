// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_MEDIA_VIDEO_BACKEND_H
#define ELG_MEDIA_VIDEO_BACKEND_H

// Media's backend interface for videos (ELGSPEC-0001.R4): each OS's media engine plays them, in
// platform/<os>/video_source.*. A VideoSource (media/video_source.h) owns each engine and keeps
// the state views read; the engine reports to it through a FrameSourceHost.

#include "media/video_frame.h"

#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>

namespace elg::detail {

// What an engine reports to the VideoSource that owns it. The host outlives its engine: the
// engine may call it until the engine is destroyed (its "any thread" functions from any thread).
// What the engine posts to the UI thread holds the host weakly instead: once that has expired,
// the source and its engine are being destroyed.
class FrameSourceHost {
public:
    virtual ~FrameSourceHost() = default;
    FrameSourceHost(const FrameSourceHost&) = delete;
    FrameSourceHost& operator=(const FrameSourceHost&) = delete;
    FrameSourceHost(FrameSourceHost&&) = delete;
    FrameSourceHost& operator=(FrameSourceHost&&) = delete;

    // --- Any thread ---

    // The source's id, for its frames' VideoFrame::source_id.
    [[nodiscard]] virtual std::uint64_t source_id() const noexcept = 0;
    // A new id, unique among sources and the textures decoders share (VideoFrame::texture_id).
    [[nodiscard]] virtual std::uint64_t new_id() const = 0;
    // Whether frames keep coming (VideoSource::is_playing_now()).
    virtual void set_playing_now(bool playing) noexcept = 0;
    [[nodiscard]] virtual bool is_playing_now() const noexcept = 0;
    // The engine has a new frame for update(): wakes the render thread, when a window shows the
    // source, to draw it (or the lack of one).
    virtual void notify_new_frame() const = 0;
    // The GPU adapter of the running renderer, once its device exists.
    [[nodiscard]] virtual std::optional<RenderAdapter> render_adapter() const = 0;

    // --- UI thread: the state views read ---

    virtual void set_playing(bool playing) = 0;
    // The metadata loaded: the duration and size are known.
    virtual void set_ready() = 0;
    virtual void set_time(double seconds) = 0;
    virtual void set_duration(double seconds) = 0;
    [[nodiscard]] virtual double duration() const = 0;
    virtual void set_size(float width, float height) = 0; // pixels
    virtual void set_volume(float volume) = 0;
    virtual void set_muted(bool muted) = 0;
    virtual void set_looping(bool looping) = 0;
    [[nodiscard]] virtual bool looping() const = 0;
    virtual void set_error(std::string message) = 0;

protected:
    FrameSourceHost() = default;
};

// A video as the OS's media engine plays it: each OS's video_source.* implements it. Its
// VideoSource owns it, destroys it on the UI thread, and calls it as VideoSource's own functions
// are called.
class VideoEngine {
public:
    VideoEngine() = default;
    VideoEngine(const VideoEngine&) = delete;
    VideoEngine& operator=(const VideoEngine&) = delete;
    VideoEngine(VideoEngine&&) = delete;
    VideoEngine& operator=(VideoEngine&&) = delete;
    virtual ~VideoEngine() = default;

    // Whether it calls FrameSourceHost::notify_new_frame() for each new frame; otherwise the
    // render thread looks for frames regularly while it plays.
    [[nodiscard]] virtual bool wakes_for_frames() const noexcept = 0;
    // UI thread, once, before anything else: starts loading the video, and reports to `host`.
    virtual void open(const std::shared_ptr<FrameSourceHost>& host) = 0;

    // --- UI thread ---
    virtual void play() = 0;
    virtual void pause() = 0;
    virtual void seek(double seconds) = 0;
    virtual void set_volume(float volume) = 0;
    virtual void set_muted(bool muted) = 0;
    virtual void set_looping(bool looping) = 0;

    // --- Render thread ---
    // Takes a new frame from the decoder if one is due; true if the current frame changed.
    virtual bool update() = 0;
    [[nodiscard]] virtual std::optional<VideoFrame> current() const = 0;
};

// UI thread: the engine that plays `url` (a file path or an http(s) URL), or why this system
// can't play videos.
std::expected<std::unique_ptr<VideoEngine>, std::string> create_video_engine(const std::string& url);

} // namespace elg::detail

#endif // ELG_MEDIA_VIDEO_BACKEND_H
