// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_VIDEO_H
#define ELG_VIDEO_H

// Video playback (design.md §13.3).
//
//   const VideoPlayer player("assets/intro.mp4");          // a file path or an http(s) URL
//   ZStack({.alignment = Alignment::bottom},
//          VideoView(player).aspect_ratio(16.f / 9.f).clip_shape(RoundedRectangle(12)),
//          VideoControls(player).padding(10))
//   player.play();
//
// The system's media stack decodes it (Windows: Media Foundation, with hardware decoding: MP4 /
// H.264, HEVC, VP9, and AV1 where the codecs are installed; MOV, WMV, ...), plays its sound, and
// keeps the two in sync. Frames go straight to the GPU, and a playing video redraws without
// laying out the window again.

#include <elg/export.h>
#include <elg/geometry.h>
#include <elg/reactive.h>
#include <elg/view.h>

#include <filesystem>
#include <memory>
#include <string>

namespace elg {

namespace detail {
class VideoSource;
} // namespace detail

// A video and its playback. Copies control the same video; it stops when the last copy (and the
// last view showing it) goes. Build players on the UI thread.
class ELG_API VideoPlayer {
public:
    // A file path or an http(s) URL. Loading is asynchronous: errors show in error().
    explicit VideoPlayer(const std::string& source);
    explicit VideoPlayer(const std::filesystem::path& file);
    explicit VideoPlayer(const char* source) : VideoPlayer(std::string(source)) {}

    void play() const;
    void pause() const;
    void toggle() const;
    void stop() const; // pauses and goes back to the start
    void seek(double seconds) const;
    void set_volume(float volume) const; // 0...1
    void set_muted(bool muted) const;
    void set_looping(bool looping) const;

    // Reactive (tracked reads).
    bool is_playing() const;
    bool is_ready() const; // the duration and size are known
    double time() const;   // seconds
    double duration() const;
    Size natural_size() const; // pixels; zero until ready
    float volume() const;
    bool is_muted() const;
    bool is_looping() const;
    // Why it can't play (unsupported format, missing file, network error); empty when fine.
    std::string error() const;

    // For Sliders: progress 0...1 (writing seeks), and volume.
    Binding<float> progress_binding() const;
    Binding<float> volume_binding() const;

    // Internal: the source the views draw.
    const std::shared_ptr<detail::VideoSource>& source() const noexcept { return source_; }

private:
    ELG_PRIVATE_MEMBERS_BEGIN
    std::shared_ptr<detail::VideoSource> source_;
    ELG_PRIVATE_MEMBERS_END
};

// The video's picture, fitted into (or filling, clipped to) the space offered, on black. Its
// ideal size is the video's own.
ELG_API View VideoView(VideoPlayer player, ContentMode mode = ContentMode::fit);

// Built-in controls: play/pause, a scrubber, the time, and mute. Lay them over (or under) a
// VideoView.
ELG_API View VideoControls(VideoPlayer player);

} // namespace elg

#endif // ELG_VIDEO_H
