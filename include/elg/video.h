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

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace elg {

namespace detail {
class VideoSource;
} // namespace detail

/// A video and its playback. Copies control the same video; it stops when the last copy (and the
/// last view showing it) goes. Build players on the UI thread.
class ELG_API VideoPlayer {
public:
    /// Opens a video, paused at the start. Loading is asynchronous: errors show in error().
    ///
    /// @param source A file path (relative to the working directory, or absolute) or an http(s) URL.
    explicit VideoPlayer(const std::string& source);
    /// Like VideoPlayer(const std::string&), for a file.
    ///
    /// @param file The video file.
    explicit VideoPlayer(const std::filesystem::path& file);
    /// Like VideoPlayer(const std::string&).
    ///
    /// @param source A file path or an http(s) URL; not null.
    explicit VideoPlayer(const char* source) : VideoPlayer(std::string(source)) {}

    /// Starts or resumes playing.
    void play() const;
    /// Pauses, keeping the position.
    void pause() const;
    /// Pauses while playing, plays otherwise.
    void toggle() const;
    /// Pauses and goes back to the start.
    void stop() const;
    /// Moves the playback position.
    ///
    /// @param seconds Seconds from the start; negative counts as 0.
    void seek(double seconds) const;
    /// @param volume 0...1; clamped, on every platform (volume() returns the clamped value).
    /// @throws UsageError When `volume` is NaN.
    void set_volume(float volume) const;
    /// @param muted Whether the sound is off, keeping the volume.
    void set_muted(bool muted) const;
    /// @param looping Whether playing past the end starts over.
    void set_looping(bool looping) const;

    // Reactive (tracked reads).

    /// @return Whether it's playing.
    [[nodiscard]] bool is_playing() const;
    /// @return Whether the duration and size are known.
    [[nodiscard]] bool is_ready() const;
    /// @return The playback position in seconds.
    [[nodiscard]] double time() const;
    /// @return The length in seconds; 0 until ready.
    [[nodiscard]] double duration() const;
    /// @return The video's size in pixels; zero until ready.
    [[nodiscard]] Size natural_size() const;
    /// @return The volume, 0...1.
    [[nodiscard]] float volume() const;
    /// @return Whether the sound is muted.
    [[nodiscard]] bool is_muted() const;
    /// @return Whether it loops.
    [[nodiscard]] bool is_looping() const;
    /// @return Why it can't play (unsupported format, missing file, network error); empty when fine.
    [[nodiscard]] std::string error() const;

    /// For Sliders: progress 0...1 (writing seeks).
    ///
    /// @return A binding to the position as a fraction of the duration; 0 until it's known.
    [[nodiscard]] Binding<float> progress_binding() const;
    /// For Sliders: the volume (writing sets it).
    ///
    /// @return A binding to the volume, 0...1.
    [[nodiscard]] Binding<float> volume_binding() const;

    /// Internal: the source the views draw.
    ///
    /// @return The player's source.
    [[nodiscard]] const std::shared_ptr<detail::VideoSource>& source() const noexcept { return source_; }

private:
    ELG_PRIVATE_MEMBERS_BEGIN
    std::shared_ptr<detail::VideoSource> source_;
    ELG_PRIVATE_MEMBERS_END
};

/// The video's picture, fitted into (or filling, clipped to) the space offered, on black. Its
/// ideal size is the video's own (320 x 180 until it's known).
///
/// @param player The video to show; the view keeps it alive.
/// @param mode ContentMode::fit shows all of the picture; ContentMode::fill crops it to the view.
/// @return The view.
ELG_API View VideoView(const VideoPlayer& player, ContentMode mode = ContentMode::fit);

/// Built-in controls: play/pause, a scrubber, the time, and mute. Lay them over (or under) a
/// VideoView. White on a translucent black capsule; the time reads "Can't play this video" when
/// the player has an error.
///
/// @param player The video they control.
/// @return The controls.
ELG_API View VideoControls(const VideoPlayer& player);

// --- Frames from the app ---

/// Video the app makes itself (a decoder of its own, a screen capture), shown by VideoFeedView:
///
/// @code
/// const VideoFeed feed;
/// VideoFeedView(feed).aspect_ratio(16.f / 9.f)
/// feed.push(VideoFeed::Format::nv12, 1280, 720, std::move(pixels));  // from any thread
/// @endcode
///
/// Frames are packed (no row padding), top to bottom. The renderer converts YUV on the GPU and
/// redraws only the views showing the feed when a frame arrives. Copies share the feed.
class ELG_API VideoFeed {
public:
    /// How a frame's pixels are laid out.
    enum class Format {
        bgra, ///< 4 bytes per pixel: blue, green, red, alpha.
        nv12, ///< A Y plane, then interleaved U and V at half the width and height.
        i420  ///< A Y plane, then a U plane and a V plane at half the width and height.
    };

    /// A feed with no frame yet.
    VideoFeed();

    /// A new frame (any thread). YUV frames are converted with BT.709 from 720 rows up, BT.601 below.
    /// A frame with no width or height, or fewer bytes than its size needs, is ignored.
    ///
    /// @param format How `pixels` is laid out.
    /// @param width The frame's width in pixels.
    /// @param height The frame's height in pixels.
    /// @param pixels The frame, packed (no row padding), top to bottom.
    /// @param full_range YUV in 0...255 rather than 16...235.
    void push(Format format, std::uint32_t width, std::uint32_t height, std::vector<std::uint8_t> pixels,
              bool full_range = false) const;
    /// No picture (black) until the next push (any thread).
    void clear() const;

    // Reactive (UI thread).

    /// @return Pixels of the frames; zero before the first and after clear().
    [[nodiscard]] Size size() const;
    /// @return false before the first push and after clear().
    [[nodiscard]] bool has_frame() const;

    /// The same feed (copies of one another).
    ///
    /// @param other The feed to compare with.
    /// @return Whether this and `other` share a feed.
    bool operator==(const VideoFeed& other) const noexcept { return source_ == other.source_; }

    /// Internal: the source the views draw.
    ///
    /// @return The feed's source.
    [[nodiscard]] const std::shared_ptr<detail::VideoSource>& source() const noexcept { return source_; }

private:
    ELG_PRIVATE_MEMBERS_BEGIN
    std::shared_ptr<detail::VideoSource> source_;
    ELG_PRIVATE_MEMBERS_END
};

/// The feed's picture, fitted into (or filling, clipped to) the space offered, on black. Its ideal
/// size is the frames' (320 x 180 before the first).
///
/// @param feed The feed to show; the view keeps it alive.
/// @param mode ContentMode::fit shows all of the picture; ContentMode::fill crops it to the view.
/// @param mirrored Shows it flipped left to right (someone's own camera).
/// @return The view.
ELG_API View VideoFeedView(const VideoFeed& feed, ContentMode mode = ContentMode::fit, bool mirrored = false);

} // namespace elg

#endif // ELG_VIDEO_H
