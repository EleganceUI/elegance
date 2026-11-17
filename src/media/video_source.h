// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_MEDIA_VIDEO_SOURCE_H
#define ELG_MEDIA_VIDEO_SOURCE_H

#include "media/frame_source.h"
#include "media/video_frame.h"

#include <elg/geometry.h>
#include <elg/reactive.h>

#include <memory>
#include <optional>
#include <string>

namespace elg::detail {

// Published by the render thread once its device exists (RenderAdapter, media/video_frame.h).
void set_render_adapter(RenderAdapter adapter);
std::optional<RenderAdapter> render_adapter();

// How the renderer imports frames of VideoFrame::Kind::native (FrameSourceHost's functions of
// those names): set by the render thread before its device exists. Any thread.
struct NativeFrameImporter {
    bool (*imports_native_frames)() = nullptr;
    std::optional<VideoFrame> (*import_native_frame)(const VideoFrame& frame) = nullptr;
};
void set_native_frame_importer(NativeFrameImporter importer);

// A video being played (design.md §13.3), shared by VideoPlayer copies and the views showing it:
// what they ask of it, and what they read. The platform's media engine decodes it, plays its
// sound, and keeps them in sync (media/video_backend.h). Its frames reach the renderer through
// its FrameSource. UI thread.
class VideoSource : public std::enable_shared_from_this<VideoSource> {
public:
    // `url` is a file path or an http(s) URL.
    static std::shared_ptr<VideoSource> create(const std::string& url);
    // A source that never plays, with `message` as its error.
    static std::shared_ptr<VideoSource> unsupported(std::string message);
    VideoSource(const VideoSource&) = delete;
    VideoSource& operator=(const VideoSource&) = delete;
    VideoSource(VideoSource&&) = delete;
    VideoSource& operator=(VideoSource&&) = delete;
    virtual ~VideoSource() = default;

    virtual void play() = 0;
    virtual void pause() = 0;
    virtual void seek(double seconds) = 0;
    virtual void set_volume(float volume) = 0;
    virtual void set_muted(bool muted) = 0;
    virtual void set_looping(bool looping) = 0;

    // What views read; updated from the engine's events.
    State<bool> playing{false};
    State<bool> ready{false}; // metadata loaded: duration and size known
    State<double> time{0.0};
    State<double> duration{0.0};
    State<Size> size{Size{}}; // pixels
    State<float> volume{1.f};
    State<bool> muted{false};
    State<bool> looping{false};
    State<std::string> error{std::string()};

    // Its frames, for the display list and the render thread.
    [[nodiscard]] virtual std::shared_ptr<FrameSource> frames() = 0;

protected:
    VideoSource() = default;
};

} // namespace elg::detail

#endif // ELG_MEDIA_VIDEO_SOURCE_H
