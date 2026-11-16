// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_MEDIA_FRAME_SOURCE_H
#define ELG_MEDIA_FRAME_SOURCE_H

// Where the renderer takes a video's, camera's, video feed's or GPU image's frames from: what the
// display list names (VideoCommand).

#include "media/video_frame.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <optional>

namespace elg::detail {

// A new id, unique among sources and the textures decoders share (VideoFrame::texture_id).
std::uint64_t next_media_id();

// What FrameSource::notify_new_frame() calls to wake the render thread: set by the render thread
// while it runs, and cleared (an empty function) before it stops. Any thread.
void set_frame_waker(std::function<void()> waker);

// The frames of a video, camera, video feed or GPU image, for the render thread. It holds no
// state of the UI thread: what views read is its VideoSource's (media/video_source.h).
class FrameSource {
public:
    FrameSource(const FrameSource&) = delete;
    FrameSource& operator=(const FrameSource&) = delete;
    FrameSource(FrameSource&&) = delete;
    FrameSource& operator=(FrameSource&&) = delete;
    virtual ~FrameSource() = default;

    // --- Render thread ---
    // Takes a new frame from the decoder if one is due; true if the current frame changed.
    virtual bool update() = 0;
    [[nodiscard]] virtual std::optional<VideoFrame> current() const = 0;
    // Whether frames keep coming.
    [[nodiscard]] bool is_playing_now() const noexcept { return playing_now_.load(std::memory_order_relaxed); }
    // Whether it calls notify_new_frame() for each new frame; otherwise the render thread looks
    // for frames regularly while it plays.
    [[nodiscard]] bool wakes_for_frames() const noexcept { return wakes_for_frames_; }
    // Whether a window shows it (FrameSources): only then do its new frames wake the render
    // thread.
    void set_shown(bool shown) noexcept { shown_ = shown; }

    // --- Any thread ---
    // Whether frames keep coming, as is_playing_now() reports it.
    void set_playing_now(bool playing) noexcept { playing_now_ = playing; }
    // After the source's frame changed: wakes the render thread, when a window shows the source,
    // to draw its new frame (or the lack of one).
    void notify_new_frame() const;

    const std::uint64_t id;

protected:
    // `wakes_for_frames`: see wakes_for_frames().
    explicit FrameSource(bool wakes_for_frames = false);

private:
    const bool wakes_for_frames_;
    std::atomic<bool> playing_now_{false};
    std::atomic<bool> shown_{false};
};

} // namespace elg::detail

#endif // ELG_MEDIA_FRAME_SOURCE_H
