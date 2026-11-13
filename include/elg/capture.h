// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_CAPTURE_H
#define ELG_CAPTURE_H

// Capture from the microphone and the camera (design.md §13.3).
//
//   const AudioRecorder recorder;                          // the default microphone
//   Button(Text([=] { return recorder.is_recording() ? "Stop" : "Record"; }), [=] { recorder.toggle(); })
//   Progress([=] { return recorder.level(); })         // a live input meter
//   recorder.save_wav("memo.wav");
//
//   const Camera camera;                                   // the first camera
//   CameraView(camera).frame(320, 180).clip_shape(RoundedRectangle(12))
//   camera.start();
//   Image({.data = *camera.snapshot()})                    // a still, as PNG
//
// Windows asks the person for access to the microphone and camera in Settings > Privacy; when
// it's denied, error() says so. Build these on the UI thread; their reactive reads are for it too.

#include <elg/audio.h>
#include <elg/export.h>
#include <elg/geometry.h>
#include <elg/reactive.h>
#include <elg/view.h>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace elg {

namespace detail {
struct RecorderState;
class VideoSource;
} // namespace detail

// --- Microphone ---

/// The system's audio inputs (microphones, line in, virtual devices).
///
/// @return The inputs; empty when the system has no audio.
ELG_API std::vector<AudioDevice> audio_input_devices();

/// Options for AudioRecorder.
struct AudioRecorderOptions {
    /// empty: the system's default input. An id that isn't connected makes start() fail, with
    /// error() saying so.
    std::string device_id;
    std::uint32_t sample_rate = 48000; ///< Frames per second, clamped to 8000...192000.
    std::uint32_t channels = 1;        ///< 1 (mono) or 2 (stereo); other counts are clamped.
    /// Each block of samples as it arrives (interleaved, -1...1), for streaming or analysis. Runs on
    /// the audio thread: keep it short, and don't touch views or state from it. If it throws, it
    /// isn't called again and the exception is rethrown on the UI thread.
    std::function<void(std::span<const float>)> on_samples;
    /// false: samples only go to on_samples (live audio, a meter), so memory doesn't grow while it
    /// runs; duration() and samples() then stay empty.
    bool keeps_samples = true;
};

/// Records from a microphone into memory. Copies share the recording; it stops when the last copy
/// goes. Making one doesn't open the microphone; start() does.
class ELG_API AudioRecorder {
public:
    /// @param options The device, format, and what happens to the samples.
    explicit AudioRecorder(AudioRecorderOptions options = {});

    /// Starts recording, after what's already recorded. On failure, error() says why.
    void start() const;
    /// Stops recording, keeping what was recorded.
    void stop() const;
    /// Stops while recording, starts otherwise.
    void toggle() const;
    /// Drops what was recorded.
    void clear() const;

    // Reactive.

    /// @return Whether it's recording (tracked).
    [[nodiscard]] bool is_recording() const;
    /// @return Seconds recorded (tracked; updated about 15 times a second while recording).
    [[nodiscard]] double duration() const;
    /// @return The input's recent peak, 0...1, while recording (tracked); 0 when stopped.
    [[nodiscard]] float level() const;
    /// @return Why it can't record (no input, access denied); empty when fine (tracked).
    [[nodiscard]] std::string error() const;

    /// @return The sample rate in use, after clamping.
    [[nodiscard]] std::uint32_t sample_rate() const noexcept;
    /// @return The channel count in use, after clamping.
    [[nodiscard]] std::uint32_t channels() const noexcept;
    /// @return What's been recorded: interleaved samples, -1...1 (a copy).
    [[nodiscard]] std::vector<float> samples() const;
    /// Writes the recording as a 16-bit WAV file.
    ///
    /// @param file Where to write it; an existing file is replaced.
    /// @throws ResourceError If the file can't be written.
    void save_wav(const std::filesystem::path& file) const;
    /// @return The recording as a 16-bit WAV file, in memory.
    [[nodiscard]] std::vector<unsigned char> wav() const;
    /// To play it back.
    ///
    /// @return The recording so far, as a Sound.
    [[nodiscard]] Sound sound() const;

private:
    ELG_PRIVATE_MEMBERS_BEGIN
    std::shared_ptr<detail::RecorderState> state_;
    ELG_PRIVATE_MEMBERS_END
};

// --- Camera ---

/// A camera, as listed by camera_devices().
struct CameraDevice {
    std::string id;   ///< for CameraOptions::device_id
    std::string name; ///< As the system shows it.
};

/// The system's cameras. UI thread.
///
/// @return The cameras; empty when there are none.
ELG_API std::vector<CameraDevice> camera_devices();

/// Options for Camera.
struct CameraOptions {
    std::string device_id; ///< empty: the first camera
    /// The preferred width in pixels; the camera's closest mode is used.
    std::uint32_t width = 1280;
    /// The preferred height in pixels; the camera's closest mode is used.
    std::uint32_t height = 720;
    bool mirrored = true; ///< the preview shows a mirror image, as people expect of themselves
};

/// A camera's live picture. Frames go to the GPU, and views showing it redraw on their own. Copies
/// share the camera; it stops when the last copy (and the last view showing it) goes. Making one
/// doesn't start capturing; start() does.
class ELG_API Camera {
public:
    /// @param options The device, preferred resolution, and whether the preview is mirrored.
    explicit Camera(CameraOptions options = {});

    /// Starts capturing. Failures (no camera, access denied) show in error().
    void start() const;
    /// Stops capturing.
    void stop() const;

    // Reactive.

    /// @return Whether it's capturing (tracked).
    [[nodiscard]] bool is_running() const;
    /// @return Pixels of the frames (tracked); zero until the first one.
    [[nodiscard]] Size size() const;
    /// @return Why it can't capture; empty when fine (tracked).
    [[nodiscard]] std::string error() const;

    /// @return The newest frame as a PNG image (not mirrored), or nullopt before the first frame.
    [[nodiscard]] std::optional<std::vector<unsigned char>> snapshot() const;

    /// Internal: for CameraView.
    ///
    /// @return The camera's frame source.
    [[nodiscard]] const std::shared_ptr<detail::VideoSource>& source() const noexcept { return source_; }
    /// Internal: for CameraView.
    ///
    /// @return CameraOptions::mirrored.
    [[nodiscard]] bool mirrored() const noexcept { return mirrored_; }

private:
    ELG_PRIVATE_MEMBERS_BEGIN
    std::shared_ptr<detail::VideoSource> source_;
    bool mirrored_ = true;
    ELG_PRIVATE_MEMBERS_END
};

/// The camera's picture, filling (cropped to) or fitted into the space offered, on black. Without a
/// proposed size, it takes the frames' size (320 x 180 until the first frame).
///
/// @param camera The camera to show; the view keeps it alive. Mirrored if CameraOptions::mirrored.
/// @param mode ContentMode::fill crops the picture to the view; ContentMode::fit shows all of it.
/// @return The view.
ELG_API View CameraView(const Camera& camera, ContentMode mode = ContentMode::fill);

} // namespace elg

#endif // ELG_CAPTURE_H
