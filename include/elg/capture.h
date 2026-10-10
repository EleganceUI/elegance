// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_CAPTURE_H
#define ELG_CAPTURE_H

// Capture from the microphone and the camera (design.md §13.3).
//
//   const AudioRecorder recorder;                          // the default microphone
//   Button([=] { return recorder.is_recording() ? "Stop" : "Record"; }, [=] { recorder.toggle(); })
//   ProgressView([=] { return recorder.level(); })         // a live input meter
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

struct AudioDevice {
    std::string id;   // for AudioRecorderOptions::device_id
    std::string name; // as the system shows it
    bool is_default = false;
};

// The system's audio inputs (microphones, line in, virtual devices).
ELG_API std::vector<AudioDevice> audio_input_devices();

struct AudioRecorderOptions {
    std::string device_id; // empty: the system's default input
    std::uint32_t sample_rate = 48000;
    std::uint32_t channels = 1;
    // Each block of samples as it arrives (interleaved, -1...1), for streaming or analysis. Runs on
    // the audio thread: keep it short, and don't touch views or state from it.
    std::function<void(std::span<const float>)> on_samples;
};

// Records from a microphone into memory. Copies share the recording; it stops when the last copy
// goes.
class ELG_API AudioRecorder {
public:
    explicit AudioRecorder(AudioRecorderOptions options = {});

    void start() const;  // continues the recording
    void stop() const;
    void toggle() const;
    void clear() const;  // drops what was recorded

    // Reactive.
    bool is_recording() const;
    double duration() const; // seconds recorded
    float level() const;     // the input's recent peak, 0...1, while recording
    std::string error() const;

    std::uint32_t sample_rate() const noexcept;
    std::uint32_t channels() const noexcept;
    // What's been recorded: interleaved samples, -1...1.
    std::vector<float> samples() const;
    // As a 16-bit WAV file, or in memory. Throws elg::ResourceError if the file can't be written.
    void save_wav(const std::filesystem::path& file) const;
    std::vector<unsigned char> wav() const;
    // To play it back.
    Sound sound() const;

private:
    ELG_PRIVATE_MEMBERS_BEGIN
    std::shared_ptr<detail::RecorderState> state_;
    ELG_PRIVATE_MEMBERS_END
};

// --- Camera ---

struct CameraDevice {
    std::string id; // for CameraOptions::device_id
    std::string name;
};

// The system's cameras.
ELG_API std::vector<CameraDevice> camera_devices();

struct CameraOptions {
    std::string device_id; // empty: the first camera
    // The preferred resolution; the camera's closest mode is used.
    std::uint32_t width = 1280;
    std::uint32_t height = 720;
    bool mirrored = true; // the preview shows a mirror image, as people expect of themselves
};

// A camera's live picture. Frames go to the GPU, and views showing it redraw on their own. Copies
// share the camera; it stops when the last copy (and the last view showing it) goes.
class ELG_API Camera {
public:
    explicit Camera(CameraOptions options = {});

    void start() const;
    void stop() const;

    // Reactive.
    bool is_running() const;
    Size size() const; // pixels of the frames; zero until the first one
    std::string error() const;

    // The newest frame as a PNG image (not mirrored), or nullopt before the first frame.
    std::optional<std::vector<unsigned char>> snapshot() const;

    // Internal: for CameraView.
    const std::shared_ptr<detail::VideoSource>& source() const noexcept { return source_; }
    bool mirrored() const noexcept { return mirrored_; }

private:
    ELG_PRIVATE_MEMBERS_BEGIN
    std::shared_ptr<detail::VideoSource> source_;
    bool mirrored_ = true;
    ELG_PRIVATE_MEMBERS_END
};

// The camera's picture, filling (cropped to) or fitted into the space offered.
ELG_API View CameraView(Camera camera, ContentMode mode = ContentMode::fill);

} // namespace elg

#endif // ELG_CAPTURE_H
