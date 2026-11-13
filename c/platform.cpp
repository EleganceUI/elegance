// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Audio, capture, video, networking, and the desktop (dialogs, notifications, tray icons).

#include "shim.h"
#include <elg/gpu_image.h>

#include <cstddef>

using namespace elg;
using namespace elg_shim;

/// An audio device (elg::AudioDevice) or a camera (elg::CameraDevice), passed to a callback.
struct ElgDevice {
    ElgStr id;       ///< For a `device_id` argument; valid during the callback.
    ElgStr name;     ///< As the system shows it; valid during the callback.
    bool is_default; ///< The system's default device; always false for cameras.
};

// Calls `each` (args: ElgDevice*) per device.
template <class Device>
static void send_devices(const std::vector<Device>& devices, const RustFn& each) {
    for (const Device& device : devices) {
        const bool is_default = [&] {
            if constexpr (requires { device.is_default; }) {
                return device.is_default;
            } else {
                return false;
            }
        }();
        ElgDevice arg{c_str(device.id), c_str(device.name), is_default};
        each.call(&arg, nullptr);
    }
}

// --- Audio ---

/// Wraps `audio_output_devices()`: the system's audio outputs (speakers, headphones, virtual
/// devices).
///
/// @param each Borrowed closure, called once per device before this returns; args: `ElgDevice*`,
///   ret: null. Required: none is a usage error.
ELG_EXPORT void elg_audio_output_devices(ElgClosure each) {
    ELG_TRY
    const Consumed consumed(each);
    const SharedFn f = required(each, "each");
    send_devices(audio_output_devices(), *f);
    ELG_CATCH_VOID
}

/// Wraps `Sound::load()` for a file: a short sound, decoded into memory once (WAV, FLAC, or MP3).
///
/// @param file The file's path.
/// @return The sound, owned by the caller (elg_sound_free()), or nullptr with a ResourceError if
///   the file can't be read or decoded.
ELG_EXPORT Sound* elg_sound_load_file(ElgStr file) {
    ELG_TRY
    return new Sound(Sound::load(path(file)));
    ELG_CATCH(nullptr)
}
/// Wraps `Sound::load()` for encoded audio in memory.
///
/// @param data The encoded audio (WAV, FLAC, or MP3), read during the call.
/// @return The sound, owned by the caller (elg_sound_free()), or nullptr with a ResourceError if
///   it can't be decoded.
ELG_EXPORT Sound* elg_sound_load_data(ElgBytes data) {
    ELG_TRY
    return new Sound(Sound::load(bytes(data)));
    ELG_CATCH(nullptr)
}
/// Copies a sound; copies share the decoded audio.
///
/// @param sound The sound (borrowed).
/// @return The copy, owned by the caller (elg_sound_free()).
ELG_EXPORT Sound* elg_sound_clone(const Sound* sound) {
    return new Sound(*sound);
}
/// Frees a sound.
///
/// @param sound The sound (consumed); may be null.
ELG_EXPORT void elg_sound_free(Sound* sound) {
    delete sound;
}
/// Wraps `Sound::play()`: plays the sound in a new voice, so plays overlap. Without an output
/// device it does nothing.
///
/// @param sound The sound (borrowed).
/// @param volume 0 to 1, and beyond, amplified.
/// @param pitch Playback speed and pitch: 1 as recorded, 2 an octave up.
/// @param pan -1 left to 1 right.
ELG_EXPORT void elg_sound_play(const Sound* sound, float volume, float pitch, float pan) {
    ELG_TRY
    sound->play({volume, pitch, pan});
    ELG_CATCH_VOID
}
/// Wraps `Sound::duration()`.
///
/// @param sound The sound (borrowed).
/// @return The length in seconds.
ELG_EXPORT double elg_sound_duration(const Sound* sound) {
    return sound->duration();
}

/// Wraps `AudioPlayer(file)`: music, speech, anything long, decoded while it plays.
///
/// @param file The file's path (WAV, FLAC, or MP3).
/// @return The player, owned by the caller (elg_audio_player_free()), or nullptr with a
///   ResourceError if the file can't be opened or isn't audio.
ELG_EXPORT AudioPlayer* elg_audio_player_new(ElgStr file) {
    ELG_TRY
    return new AudioPlayer(path(file));
    ELG_CATCH(nullptr)
}
/// Copies a player; copies control the same player.
///
/// @param player The player (borrowed).
/// @return The copy, owned by the caller (elg_audio_player_free()).
ELG_EXPORT AudioPlayer* elg_audio_player_clone(const AudioPlayer* player) {
    return new AudioPlayer(*player);
}
/// Frees a player; it stops when the last copy goes.
///
/// @param player The player (consumed); may be null.
ELG_EXPORT void elg_audio_player_free(AudioPlayer* player) {
    delete player;
}
/// Wraps `AudioPlayer::play()`, `pause()`, `toggle()` and `stop()` (which pauses and goes back to
/// the start).
///
/// @param player The player (borrowed).
/// @param op 0 play, 1 pause, 2 toggle, 3 stop.
ELG_EXPORT void elg_audio_player_control(const AudioPlayer* player, int32_t op) {
    ELG_TRY
    switch (op) {
    case 0: player->play(); break;
    case 1: player->pause(); break;
    case 2: player->toggle(); break;
    default: player->stop(); break;
    }
    ELG_CATCH_VOID
}
/// Wraps `AudioPlayer::seek()`.
///
/// @param player The player (borrowed).
/// @param seconds Where to go, from the start; clamped to the duration.
ELG_EXPORT void elg_audio_player_seek(const AudioPlayer* player, double seconds) {
    ELG_TRY
    player->seek(seconds);
    ELG_CATCH_VOID
}
/// Wraps `AudioPlayer::set_volume()`.
///
/// @param player The player (borrowed).
/// @param volume 0 to 1, and beyond, amplified; negative counts as 0.
ELG_EXPORT void elg_audio_player_set_volume(const AudioPlayer* player, float volume) {
    ELG_TRY
    player->set_volume(volume);
    ELG_CATCH_VOID
}
/// Wraps `AudioPlayer::set_looping()`.
///
/// @param player The player (borrowed).
/// @param looping Whether it starts over when it reaches the end.
ELG_EXPORT void elg_audio_player_set_looping(const AudioPlayer* player, bool looping) {
    ELG_TRY
    player->set_looping(looping);
    ELG_CATCH_VOID
}
/// Wraps `AudioPlayer::is_playing()` and `is_looping()`. Tracked reads, for the UI thread.
///
/// @param player The player (borrowed).
/// @param which 0 playing, 1 looping.
/// @return The flag, or false on failure.
ELG_EXPORT bool elg_audio_player_flag(const AudioPlayer* player, int32_t which) {
    ELG_TRY
    return which == 0 ? player->is_playing() : player->is_looping();
    ELG_CATCH(false)
}
/// Wraps `AudioPlayer::time()`, `duration()` and `volume()`. Tracked reads, for the UI thread:
/// views showing them update while it plays.
///
/// @param player The player (borrowed).
/// @param which 0 time, 1 duration (both in seconds), 2 volume.
/// @return The value, or 0 on failure.
ELG_EXPORT double elg_audio_player_value(const AudioPlayer* player, int32_t which) {
    ELG_TRY
    switch (which) {
    case 0: return player->time();
    case 1: return player->duration();
    default: return player->volume();
    }
    ELG_CATCH(0.0)
}
/// Wraps `AudioPlayer::time_binding()` and `volume_binding()`, for a Slider: reading follows
/// playback, writing seeks or sets the volume.
///
/// @param player The player (borrowed).
/// @param which 0 time, 1 volume.
/// @return The binding, owned by the caller (elg_binding_f32_free()), or nullptr on failure.
ELG_EXPORT Binding<float>* elg_audio_player_binding(const AudioPlayer* player, int32_t which) {
    ELG_TRY
    return new Binding<float>(which == 0 ? player->time_binding() : player->volume_binding());
    ELG_CATCH(nullptr)
}

/// A buffer for an audio stream's `on_render` closure to fill.
struct ElgRenderBuffer {
    float* samples; ///< Interleaved samples, -1 to 1, zeroed beforehand; valid during the call.
    size_t count;   ///< Number of samples (frames times channels).
};

/// Wraps `AudioStream(AudioStreamOptions)`: live audio out, samples written as they're made
/// (elg_audio_stream_write()) or mixed on demand by `on_render`. It starts playing when made.
///
/// @param device_id An output's ElgDevice::id; empty: the system's default (followed when it
///   changes).
/// @param sample_rate Frames per second, clamped to 8000 to 192000.
/// @param channels 1 or 2 (clamped).
/// @param on_render Optional consumed closure (`call` null: none) that fills each buffer the device
///   asks for instead of playing what was written. `on_render` (args: ElgRenderBuffer*) runs on
///   the audio thread; ret: null. Keep it short and lock-light.
/// @return The stream, owned by the caller (elg_audio_stream_free()), or nullptr on failure.
ELG_EXPORT AudioStream* elg_audio_stream_new(ElgStr device_id, uint32_t sample_rate, uint32_t channels, ElgClosure on_render) {
    ELG_TRY
    const Consumed consumed(on_render);
    AudioStreamOptions options;
    options.device_id = str(device_id);
    options.sample_rate = sample_rate;
    options.channels = channels;
    if (const SharedFn f = share(on_render)) {
        options.on_render = [f](std::span<float> buffer) {
            ElgRenderBuffer arg{buffer.data(), buffer.size()};
            f->call(&arg, nullptr);
        };
    }
    return new AudioStream(std::move(options));
    ELG_CATCH(nullptr)
}
/// Copies a stream; copies share it.
///
/// @param stream The stream (borrowed).
/// @return The copy, owned by the caller (elg_audio_stream_free()).
ELG_EXPORT AudioStream* elg_audio_stream_clone(const AudioStream* stream) {
    return new AudioStream(*stream);
}
/// Frees a stream; it stops when the last copy goes.
///
/// @param stream The stream (consumed); may be null.
ELG_EXPORT void elg_audio_stream_free(AudioStream* stream) {
    delete stream;
}
/// Wraps `AudioStream::write()`: queues samples to play (without `on_render`). Callable from any
/// thread. Beyond about a second queued, the oldest are dropped.
///
/// @param stream The stream (borrowed).
/// @param samples Interleaved samples, -1 to 1, copied.
/// @param count Number of samples (frames times channels).
ELG_EXPORT void elg_audio_stream_write(const AudioStream* stream, const float* samples, size_t count) {
    ELG_TRY
    stream->write(std::span<const float>(samples, count));
    ELG_CATCH_VOID
}
/// Wraps `AudioStream::clear()` (drops what's queued), `start()` and `stop()` (which pauses it).
///
/// @param stream The stream (borrowed).
/// @param op 0 clear, 1 start, 2 stop.
ELG_EXPORT void elg_audio_stream_control(const AudioStream* stream, int32_t op) {
    ELG_TRY
    switch (op) {
    case 0: stream->clear(); break;
    case 1: stream->start(); break;
    default: stream->stop(); break;
    }
    ELG_CATCH_VOID
}
/// Wraps `AudioStream::queued_frames()`.
///
/// @param stream The stream (borrowed).
/// @return How many frames are queued, or 0 on failure.
ELG_EXPORT size_t elg_audio_stream_queued_frames(const AudioStream* stream) {
    ELG_TRY
    return stream->queued_frames();
    ELG_CATCH(0)
}
/// Wraps `AudioStream::set_volume()`.
///
/// @param stream The stream (borrowed).
/// @param volume 0 to 1, and beyond, amplified; negative counts as 0.
ELG_EXPORT void elg_audio_stream_set_volume(const AudioStream* stream, float volume) {
    ELG_TRY
    stream->set_volume(volume);
    ELG_CATCH_VOID
}
/// Wraps `AudioStream::error()`: why it can't play (no device, failed to open). A tracked read.
///
/// @param stream The stream (borrowed).
/// @param out Receives the error; empty when fine.
/// @param ctx Passed to `out`.
ELG_EXPORT void elg_audio_stream_error(const AudioStream* stream, ElgStrSink out, void* ctx) {
    ELG_TRY
    sink(out, ctx, stream->error());
    ELG_CATCH_VOID
}
/// Wraps `AudioStream::sample_rate()`.
///
/// @param stream The stream (borrowed).
/// @return Frames per second, after clamping.
ELG_EXPORT uint32_t elg_audio_stream_sample_rate(const AudioStream* stream) {
    return stream->sample_rate();
}
/// Wraps `AudioStream::channels()`.
///
/// @param stream The stream (borrowed).
/// @return The number of channels, after clamping.
ELG_EXPORT uint32_t elg_audio_stream_channels(const AudioStream* stream) {
    return stream->channels();
}

// --- Capture ---

/// Wraps `audio_input_devices()`: the system's audio inputs (microphones, line in, virtual
/// devices).
///
/// @param each Borrowed closure, called once per device before this returns; args: `ElgDevice*`,
///   ret: null. Required: none is a usage error.
ELG_EXPORT void elg_audio_input_devices(ElgClosure each) {
    ELG_TRY
    const Consumed consumed(each);
    const SharedFn f = required(each, "each");
    send_devices(audio_input_devices(), *f);
    ELG_CATCH_VOID
}

/// Recorded samples, passed to a callback.
struct ElgSamples {
    const float* samples; ///< Interleaved samples, -1 to 1; valid during the callback.
    size_t count;         ///< Number of samples (frames times channels).
};

/// Wraps `AudioRecorder(AudioRecorderOptions)`: records from a microphone into memory. It starts
/// with elg_audio_recorder_control(). Windows asks the person for access; when it's denied, the
/// recorder's error says so.
///
/// @param device_id An input's ElgDevice::id; empty: the system's default input.
/// @param sample_rate Frames per second, clamped to 8000 to 192000.
/// @param channels 1 or 2 (clamped).
/// @param on_samples Optional consumed closure (`call` null: none) that gets each block of
///   samples as it arrives. `on_samples` (args: ElgSamples*) runs on the audio thread; ret: null.
///   Keep it short, and don't touch views or state from it.
/// @param keeps_samples false: samples only go to `on_samples`, so memory doesn't grow while it
///   records; the duration and samples then stay empty.
/// @return The recorder, owned by the caller (elg_audio_recorder_free()), or nullptr on failure.
ELG_EXPORT AudioRecorder* elg_audio_recorder_new(ElgStr device_id, uint32_t sample_rate, uint32_t channels,
                                                 ElgClosure on_samples, bool keeps_samples) {
    ELG_TRY
    const Consumed consumed(on_samples);
    AudioRecorderOptions options;
    options.device_id = str(device_id);
    options.sample_rate = sample_rate;
    options.channels = channels;
    if (const SharedFn f = share(on_samples)) {
        options.on_samples = [f](std::span<const float> samples) {
            ElgSamples arg{samples.data(), samples.size()};
            f->call(&arg, nullptr);
        };
    }
    options.keeps_samples = keeps_samples;
    return new AudioRecorder(std::move(options));
    ELG_CATCH(nullptr)
}
/// Copies a recorder; copies share the recording.
///
/// @param recorder The recorder (borrowed).
/// @return The copy, owned by the caller (elg_audio_recorder_free()).
ELG_EXPORT AudioRecorder* elg_audio_recorder_clone(const AudioRecorder* recorder) {
    return new AudioRecorder(*recorder);
}
/// Frees a recorder; it stops when the last copy goes.
///
/// @param recorder The recorder (consumed); may be null.
ELG_EXPORT void elg_audio_recorder_free(AudioRecorder* recorder) {
    delete recorder;
}
/// Wraps `AudioRecorder::start()` (which continues the recording), `stop()`, `toggle()` and
/// `clear()` (which drops what was recorded).
///
/// @param recorder The recorder (borrowed).
/// @param op 0 start, 1 stop, 2 toggle, 3 clear.
ELG_EXPORT void elg_audio_recorder_control(const AudioRecorder* recorder, int32_t op) {
    ELG_TRY
    switch (op) {
    case 0: recorder->start(); break;
    case 1: recorder->stop(); break;
    case 2: recorder->toggle(); break;
    default: recorder->clear(); break;
    }
    ELG_CATCH_VOID
}
/// Wraps `AudioRecorder::is_recording()`. A tracked read, for the UI thread.
///
/// @param recorder The recorder (borrowed).
/// @return true while recording; false on failure.
ELG_EXPORT bool elg_audio_recorder_is_recording(const AudioRecorder* recorder) {
    ELG_TRY
    return recorder->is_recording();
    ELG_CATCH(false)
}
/// Wraps `AudioRecorder::duration()`. A tracked read, for the UI thread.
///
/// @param recorder The recorder (borrowed).
/// @return Seconds recorded, or 0 on failure.
ELG_EXPORT double elg_audio_recorder_duration(const AudioRecorder* recorder) {
    ELG_TRY
    return recorder->duration();
    ELG_CATCH(0.0)
}
/// Wraps `AudioRecorder::level()`, for a live input meter. A tracked read, for the UI thread.
///
/// @param recorder The recorder (borrowed).
/// @return The input's recent peak, 0 to 1, while recording; 0 on failure.
ELG_EXPORT float elg_audio_recorder_level(const AudioRecorder* recorder) {
    ELG_TRY
    return recorder->level();
    ELG_CATCH(0.f)
}
/// Wraps `AudioRecorder::error()`: why it can't record (e.g. access was denied). A tracked read.
///
/// @param recorder The recorder (borrowed).
/// @param out Receives the error; empty when fine.
/// @param ctx Passed to `out`.
ELG_EXPORT void elg_audio_recorder_error(const AudioRecorder* recorder, ElgStrSink out, void* ctx) {
    ELG_TRY
    sink(out, ctx, recorder->error());
    ELG_CATCH_VOID
}
/// Wraps `AudioRecorder::sample_rate()`.
///
/// @param recorder The recorder (borrowed).
/// @return Frames per second, after clamping.
ELG_EXPORT uint32_t elg_audio_recorder_sample_rate(const AudioRecorder* recorder) {
    return recorder->sample_rate();
}
/// Wraps `AudioRecorder::channels()`.
///
/// @param recorder The recorder (borrowed).
/// @return The number of channels, after clamping.
ELG_EXPORT uint32_t elg_audio_recorder_channels(const AudioRecorder* recorder) {
    return recorder->channels();
}
/// Wraps `AudioRecorder::samples()`: calls `each` (args: ElgSamples*) once with all the samples.
///
/// @param recorder The recorder (borrowed).
/// @param each Borrowed closure, called once before this returns; args: `ElgSamples*`, ret: null.
///   Required: none is a usage error.
ELG_EXPORT void elg_audio_recorder_samples(const AudioRecorder* recorder, ElgClosure each) {
    ELG_TRY
    const Consumed consumed(each);
    const SharedFn f = required(each, "each");
    const std::vector<float> samples = recorder->samples();
    ElgSamples arg{samples.data(), samples.size()};
    f->call(&arg, nullptr);
    ELG_CATCH_VOID
}
/// Wraps `AudioRecorder::save_wav()`: writes the recording as a 16-bit WAV file.
///
/// @param recorder The recorder (borrowed).
/// @param file The file's path. A ResourceError is recorded if it can't be written.
ELG_EXPORT void elg_audio_recorder_save_wav(const AudioRecorder* recorder, ElgStr file) {
    ELG_TRY
    recorder->save_wav(path(file));
    ELG_CATCH_VOID
}
/// Wraps `AudioRecorder::wav()`: calls `each` (args: ElgBytes*) once with the WAV file's bytes
/// (16-bit).
///
/// @param recorder The recorder (borrowed).
/// @param each Borrowed closure, called once before this returns; args: `ElgBytes*`, ret: null.
///   Required: none is a usage error.
ELG_EXPORT void elg_audio_recorder_wav(const AudioRecorder* recorder, ElgClosure each) {
    ELG_TRY
    const Consumed consumed(each);
    const SharedFn f = required(each, "each");
    const std::vector<unsigned char> wav = recorder->wav();
    ElgBytes arg{wav.data(), wav.size()};
    f->call(&arg, nullptr);
    ELG_CATCH_VOID
}
/// Wraps `AudioRecorder::sound()`: the recording as a Sound, to play it back.
///
/// @param recorder The recorder (borrowed).
/// @return The sound, owned by the caller (elg_sound_free()), or nullptr on failure.
ELG_EXPORT Sound* elg_audio_recorder_sound(const AudioRecorder* recorder) {
    ELG_TRY
    return new Sound(recorder->sound());
    ELG_CATCH(nullptr)
}

/// Wraps `camera_devices()`: the system's cameras.
///
/// @param each Borrowed closure, called once per camera before this returns; args: `ElgDevice*`,
///   ret: null. Required: none is a usage error.
ELG_EXPORT void elg_camera_devices(ElgClosure each) {
    ELG_TRY
    const Consumed consumed(each);
    const SharedFn f = required(each, "each");
    send_devices(camera_devices(), *f);
    ELG_CATCH_VOID
}

/// Wraps `Camera(CameraOptions)`: a camera's live picture, for elg_camera_view(). It starts with
/// elg_camera_start().
///
/// @param device_id A camera's ElgDevice::id; empty: the first camera.
/// @param width Preferred frame width in pixels; the camera's closest mode is used.
/// @param height Preferred frame height in pixels.
/// @param mirrored Whether the preview shows a mirror image, as people expect of themselves.
/// @return The camera, owned by the caller (elg_camera_free()), or nullptr on failure.
ELG_EXPORT Camera* elg_camera_new(ElgStr device_id, uint32_t width, uint32_t height, bool mirrored) {
    ELG_TRY
    return new Camera({str(device_id), width, height, mirrored});
    ELG_CATCH(nullptr)
}
/// Copies a camera; copies share it.
///
/// @param camera The camera (borrowed).
/// @return The copy, owned by the caller (elg_camera_free()).
ELG_EXPORT Camera* elg_camera_clone(const Camera* camera) {
    return new Camera(*camera);
}
/// Frees a camera; it stops when the last copy (and the last view showing it) goes.
///
/// @param camera The camera (consumed); may be null.
ELG_EXPORT void elg_camera_free(Camera* camera) {
    delete camera;
}
/// Wraps `Camera::start()` and `stop()`.
///
/// @param camera The camera (borrowed).
/// @param start true to start, false to stop.
ELG_EXPORT void elg_camera_start(const Camera* camera, bool start) {
    ELG_TRY
    if (start) {
        camera->start();
    } else {
        camera->stop();
    }
    ELG_CATCH_VOID
}
/// Wraps `Camera::is_running()`. A tracked read, for the UI thread.
///
/// @param camera The camera (borrowed).
/// @return true while it runs; false on failure.
ELG_EXPORT bool elg_camera_is_running(const Camera* camera) {
    ELG_TRY
    return camera->is_running();
    ELG_CATCH(false)
}
/// Wraps `Camera::size()`. A tracked read, for the UI thread.
///
/// @param camera The camera (borrowed).
/// @return The frames' size in pixels; zero until the first one, and on failure.
ELG_EXPORT ElgSize elg_camera_size(const Camera* camera) {
    ELG_TRY
    const Size s = camera->size();
    return {s.width, s.height};
    ELG_CATCH(ElgSize{})
}
/// Wraps `Camera::error()`: why it can't run (e.g. access was denied). A tracked read.
///
/// @param camera The camera (borrowed).
/// @param out Receives the error; empty when fine.
/// @param ctx Passed to `out`.
ELG_EXPORT void elg_camera_error(const Camera* camera, ElgStrSink out, void* ctx) {
    ELG_TRY
    sink(out, ctx, camera->error());
    ELG_CATCH_VOID
}
/// Wraps `Camera::mirrored()`.
///
/// @param camera The camera (borrowed).
/// @return Whether the preview shows a mirror image.
ELG_EXPORT bool elg_camera_mirrored(const Camera* camera) {
    return camera->mirrored();
}
/// Wraps `Camera::snapshot()`: calls `each` (args: ElgBytes*) with a PNG of the latest frame
/// (not mirrored); returns false if there's none.
///
/// @param camera The camera (borrowed).
/// @param each Borrowed closure, called at most once before this returns; args: `ElgBytes*`, ret:
///   null. Required: none is a usage error.
/// @return true if `each` got a frame; false before the first frame, and on failure.
ELG_EXPORT bool elg_camera_snapshot(const Camera* camera, ElgClosure each) {
    ELG_TRY
    const Consumed consumed(each);
    const SharedFn f = required(each, "each");
    const auto png = camera->snapshot();
    if (!png) {
        return false;
    }
    ElgBytes arg{png->data(), png->size()};
    f->call(&arg, nullptr);
    return true;
    ELG_CATCH(false)
}
/// Wraps `CameraView()`: the camera's picture, filling (cropped to) or fitted into the space
/// offered.
///
/// @param camera The camera (borrowed; the view shares it).
/// @param mode An elg::ContentMode: 0 fit, 1 fill (the C++ default).
/// @return The new view, owned by the caller, or nullptr on failure.
ELG_EXPORT View* elg_camera_view(const Camera* camera, int32_t mode) {
    ELG_TRY
    return out(CameraView(*camera, static_cast<ContentMode>(mode)));
    ELG_CATCH(nullptr)
}

// --- Video ---

/// Wraps `VideoPlayer(const std::string&)`: a video and its playback. Build players on the UI
/// thread. Loading is asynchronous: errors show in elg_video_player_error().
///
/// @param source A file path or an http(s) URL.
/// @return The player, owned by the caller (elg_video_player_free()), or nullptr on failure.
ELG_EXPORT VideoPlayer* elg_video_player_new(ElgStr source) {
    ELG_TRY
    return new VideoPlayer(str(source));
    ELG_CATCH(nullptr)
}
/// Wraps `VideoPlayer(const std::filesystem::path&)`: like elg_video_player_new(), for a file.
///
/// @param file The file's path.
/// @return The player, owned by the caller (elg_video_player_free()), or nullptr on failure.
ELG_EXPORT VideoPlayer* elg_video_player_new_file(ElgStr file) {
    ELG_TRY
    return new VideoPlayer(path(file));
    ELG_CATCH(nullptr)
}
/// Copies a player; copies control the same video.
///
/// @param player The player (borrowed).
/// @return The copy, owned by the caller (elg_video_player_free()).
ELG_EXPORT VideoPlayer* elg_video_player_clone(const VideoPlayer* player) {
    return new VideoPlayer(*player);
}
/// Frees a player; the video stops when the last copy (and the last view showing it) goes.
///
/// @param player The player (consumed); may be null.
ELG_EXPORT void elg_video_player_free(VideoPlayer* player) {
    delete player;
}
/// Wraps `VideoPlayer::play()`, `pause()`, `toggle()` and `stop()` (which pauses and goes back to
/// the start).
///
/// @param player The player (borrowed).
/// @param op 0 play, 1 pause, 2 toggle, 3 stop.
ELG_EXPORT void elg_video_player_control(const VideoPlayer* player, int32_t op) {
    ELG_TRY
    switch (op) {
    case 0: player->play(); break;
    case 1: player->pause(); break;
    case 2: player->toggle(); break;
    default: player->stop(); break;
    }
    ELG_CATCH_VOID
}
/// Wraps `VideoPlayer::seek()`.
///
/// @param player The player (borrowed).
/// @param seconds Where to go, from the start.
ELG_EXPORT void elg_video_player_seek(const VideoPlayer* player, double seconds) {
    ELG_TRY
    player->seek(seconds);
    ELG_CATCH_VOID
}
/// Wraps `VideoPlayer::set_volume()`.
///
/// @param player The player (borrowed).
/// @param volume 0 to 1.
ELG_EXPORT void elg_video_player_set_volume(const VideoPlayer* player, float volume) {
    ELG_TRY
    player->set_volume(volume);
    ELG_CATCH_VOID
}
/// Wraps `VideoPlayer::set_muted()`.
///
/// @param player The player (borrowed).
/// @param muted Whether its sound is muted.
ELG_EXPORT void elg_video_player_set_muted(const VideoPlayer* player, bool muted) {
    ELG_TRY
    player->set_muted(muted);
    ELG_CATCH_VOID
}
/// Wraps `VideoPlayer::set_looping()`.
///
/// @param player The player (borrowed).
/// @param looping Whether it starts over when it reaches the end.
ELG_EXPORT void elg_video_player_set_looping(const VideoPlayer* player, bool looping) {
    ELG_TRY
    player->set_looping(looping);
    ELG_CATCH_VOID
}
/// Wraps `VideoPlayer::is_playing()`, `is_ready()` (the duration and size are known), `is_muted()`
/// and `is_looping()`. Tracked reads, for the UI thread.
///
/// @param player The player (borrowed).
/// @param which 0 playing, 1 ready, 2 muted, 3 looping.
/// @return The flag, or false on failure.
ELG_EXPORT bool elg_video_player_flag(const VideoPlayer* player, int32_t which) {
    ELG_TRY
    switch (which) {
    case 0: return player->is_playing();
    case 1: return player->is_ready();
    case 2: return player->is_muted();
    default: return player->is_looping();
    }
    ELG_CATCH(false)
}
/// Wraps `VideoPlayer::time()`, `duration()` and `volume()`. Tracked reads, for the UI thread.
///
/// @param player The player (borrowed).
/// @param which 0 time, 1 duration (both in seconds), 2 volume.
/// @return The value, or 0 on failure.
ELG_EXPORT double elg_video_player_value(const VideoPlayer* player, int32_t which) {
    ELG_TRY
    switch (which) {
    case 0: return player->time();
    case 1: return player->duration();
    default: return player->volume();
    }
    ELG_CATCH(0.0)
}
/// Wraps `VideoPlayer::natural_size()`. A tracked read, for the UI thread.
///
/// @param player The player (borrowed).
/// @return The video's size in pixels; zero until it's ready, and on failure.
ELG_EXPORT ElgSize elg_video_player_natural_size(const VideoPlayer* player) {
    ELG_TRY
    const Size s = player->natural_size();
    return {s.width, s.height};
    ELG_CATCH(ElgSize{})
}
/// Wraps `VideoPlayer::error()`: why it can't play (unsupported format, missing file, network
/// error). A tracked read.
///
/// @param player The player (borrowed).
/// @param out Receives the error; empty when fine.
/// @param ctx Passed to `out`.
ELG_EXPORT void elg_video_player_error(const VideoPlayer* player, ElgStrSink out, void* ctx) {
    ELG_TRY
    sink(out, ctx, player->error());
    ELG_CATCH_VOID
}
/// Wraps `VideoPlayer::progress_binding()` (0 to 1; writing seeks) and `volume_binding()`, for
/// Sliders.
///
/// @param player The player (borrowed).
/// @param which 0 progress, 1 volume.
/// @return The binding, owned by the caller (elg_binding_f32_free()), or nullptr on failure.
ELG_EXPORT Binding<float>* elg_video_player_binding(const VideoPlayer* player, int32_t which) {
    ELG_TRY
    return new Binding<float>(which == 0 ? player->progress_binding() : player->volume_binding());
    ELG_CATCH(nullptr)
}
/// Wraps `VideoView()`: the video's picture, fitted into (or filling, clipped to) the space
/// offered, on black. Its ideal size is the video's own.
///
/// @param player The player (borrowed; the view shares it).
/// @param mode An elg::ContentMode: 0 fit (the C++ default), 1 fill.
/// @return The new view, owned by the caller, or nullptr on failure.
ELG_EXPORT View* elg_video_view(const VideoPlayer* player, int32_t mode) {
    ELG_TRY
    return out(VideoView(*player, static_cast<ContentMode>(mode)));
    ELG_CATCH(nullptr)
}
/// Wraps `VideoControls()`: play/pause, a scrubber, the time, and mute, to lay over (or under) a
/// video view.
///
/// @param player The player (borrowed; the view shares it).
/// @return The new view, owned by the caller, or nullptr on failure.
ELG_EXPORT View* elg_video_controls(const VideoPlayer* player) {
    ELG_TRY
    return out(VideoControls(*player));
    ELG_CATCH(nullptr)
}

// --- External GPU images ---

/// Wraps `GpuImageView()`: shows the frames published to an ElgGpuImage (elg/gpu_image.h).
///
/// @param image The image (borrowed; the view keeps its source alive independently of the
///   handle); null: an empty view.
/// @param mode An elg::ContentMode: 0 fit, 1 fill.
/// @return The new view, owned by the caller, or nullptr on failure.
ELG_EXPORT elg::View* elg_gpu_image_view(const ElgGpuImage* image, int32_t mode) {
    ELG_TRY
    return new elg::View(elg::GpuImageView(image, static_cast<elg::ContentMode>(mode)));
    ELG_CATCH(nullptr)
}

/// Wraps `VideoFeed()`: video the app makes itself (a decoder of its own, a screen capture), shown
/// by elg_video_feed_view().
///
/// @return The feed, owned by the caller (elg_video_feed_free()), or nullptr on failure.
ELG_EXPORT VideoFeed* elg_video_feed_new() {
    ELG_TRY
    return new VideoFeed();
    ELG_CATCH(nullptr)
}
/// Copies a feed; copies share it.
///
/// @param feed The feed (borrowed).
/// @return The copy, owned by the caller (elg_video_feed_free()).
ELG_EXPORT VideoFeed* elg_video_feed_clone(const VideoFeed* feed) {
    return new VideoFeed(*feed);
}
/// Frees a feed.
///
/// @param feed The feed (consumed); may be null.
ELG_EXPORT void elg_video_feed_free(VideoFeed* feed) {
    delete feed;
}
/// Whether two feeds are the same feed (copies of one another): `VideoFeed`'s operator==.
///
/// @param a A feed (borrowed).
/// @param b Another feed (borrowed).
/// @return true for the same feed.
ELG_EXPORT bool elg_video_feed_eq(const VideoFeed* a, const VideoFeed* b) {
    return *a == *b;
}
/// Wraps `VideoFeed::push()`: a new frame. Callable from any thread. Frames are packed (no row
/// padding), top to bottom. A frame with a zero dimension, or fewer bytes than its size needs, is
/// ignored.
///
/// @param feed The feed (borrowed).
/// @param format A VideoFeed::Format: 0 bgra, 1 nv12, 2 i420.
/// @param width Width in pixels.
/// @param height Height in pixels.
/// @param pixels The frame's bytes, copied.
/// @param count Number of bytes at `pixels`.
/// @param full_range YUV in 0...255 rather than 16...235.
ELG_EXPORT void elg_video_feed_push(const VideoFeed* feed, int32_t format, uint32_t width, uint32_t height,
                                    const uint8_t* pixels, size_t count, bool full_range) {
    ELG_TRY
    feed->push(static_cast<VideoFeed::Format>(format), width, height, std::vector<std::uint8_t>(pixels, pixels + count),
               full_range);
    ELG_CATCH_VOID
}
/// Wraps `VideoFeed::clear()`: no picture (black) until the next push. Callable from any thread.
///
/// @param feed The feed (borrowed).
ELG_EXPORT void elg_video_feed_clear(const VideoFeed* feed) {
    ELG_TRY
    feed->clear();
    ELG_CATCH_VOID
}
/// Wraps `VideoFeed::size()`. A tracked read, for the UI thread.
///
/// @param feed The feed (borrowed).
/// @return The frames' size in pixels; zero before the first, and on failure.
ELG_EXPORT ElgSize elg_video_feed_size(const VideoFeed* feed) {
    ELG_TRY
    const Size s = feed->size();
    return {s.width, s.height};
    ELG_CATCH(ElgSize{})
}
/// Wraps `VideoFeed::has_frame()`. A tracked read, for the UI thread.
///
/// @param feed The feed (borrowed).
/// @return false before the first push, after elg_video_feed_clear(), and on failure.
ELG_EXPORT bool elg_video_feed_has_frame(const VideoFeed* feed) {
    ELG_TRY
    return feed->has_frame();
    ELG_CATCH(false)
}
/// Wraps `VideoFeedView()`: the feed's picture, fitted into (or filling, clipped to) the space
/// offered, on black.
///
/// @param feed The feed (borrowed; the view shares it).
/// @param mode An elg::ContentMode: 0 fit (the C++ default), 1 fill.
/// @param mirrored Whether to show it flipped left to right (someone's own camera).
/// @return The new view, owned by the caller, or nullptr on failure.
ELG_EXPORT View* elg_video_feed_view(const VideoFeed* feed, int32_t mode, bool mirrored) {
    ELG_TRY
    return out(VideoFeedView(*feed, static_cast<ContentMode>(mode), mirrored));
    ELG_CATCH(nullptr)
}

// --- Desktop ---

/// An elg::FileFilter: a choice in a file dialog's type list, e.g. "Images" with png and jpg.
struct ElgFileFilter {
    ElgStr name;              ///< What the dialog shows.
    const ElgStr* extensions; ///< Extensions without the dot, read during the call; none: all files.
    size_t extension_count;   ///< Number of `extensions`.
};

/// Paths a dialog's `done` closure gets.
struct ElgPaths {
    const ElgStr* paths; ///< UTF-8 paths, valid during the callback.
    size_t count;        ///< Number of `paths`; 0 if the dialog was cancelled.
};

static std::vector<FileFilter> filters(const ElgFileFilter* items, size_t count) {
    std::vector<FileFilter> result;
    for (size_t i = 0; i < count; ++i) {
        FileFilter filter{str(items[i].name), {}};
        for (size_t j = 0; j < items[i].extension_count; ++j) {
            filter.extensions.push_back(str(items[i].extensions[j]));
        }
        result.push_back(std::move(filter));
    }
    return result;
}

static void send_paths(const SharedFn& f, const std::vector<std::filesystem::path>& paths) {
    std::vector<std::string> names;
    names.reserve(paths.size());
    for (const auto& p : paths) {
        names.push_back(utf8(p));
    }
    std::vector<ElgStr> items;
    items.reserve(names.size());
    for (const auto& n : names) {
        items.push_back(c_str(n));
    }
    ElgPaths arg{items.data(), items.size()};
    f->call(&arg, nullptr);
}

/// Wraps `choose_files()`: the system's open dialog, over the active window. Call it on the UI
/// thread of a running App; otherwise it records a UsageError.
///
/// @param fs The file type filters, read during the call.
/// @param filter_count Number of `fs`.
/// @param directory Where the dialog opens; empty: the system's choice.
/// @param multiple Whether several files can be chosen.
/// @param done Consumed closure, called once on the UI thread when the dialog closes. `done` args:
///   ElgPaths* (empty if cancelled). ret: null. Required: none is a usage error.
ELG_EXPORT void elg_choose_files(const ElgFileFilter* fs, size_t filter_count, ElgStr directory, bool multiple, ElgClosure done) {
    ELG_TRY
    const Consumed consumed(done);
    const SharedFn f = required(done, "done");
    choose_files({filters(fs, filter_count), path(directory), multiple},
                 [f](const std::vector<std::filesystem::path>& paths) { send_paths(f, paths); });
    ELG_CATCH_VOID
}
/// Wraps `choose_save_file()`: the system's save dialog. The file isn't created; the dialog asked
/// about replacing an existing one. Call it on the UI thread of a running App; otherwise it records
/// a UsageError.
///
/// @param fs The file type filters, read during the call.
/// @param filter_count Number of `fs`.
/// @param directory Where the dialog opens; empty: the system's choice.
/// @param name The suggested file name.
/// @param done Consumed closure, called once on the UI thread when the dialog closes; args:
///   `ElgPaths*`, the chosen path (none if cancelled); ret: null. Required: none is a usage error.
ELG_EXPORT void elg_choose_save_file(const ElgFileFilter* fs, size_t filter_count, ElgStr directory, ElgStr name,
                                     ElgClosure done) {
    ELG_TRY
    const Consumed consumed(done);
    const SharedFn f = required(done, "done");
    choose_save_file({filters(fs, filter_count), path(directory), str(name)}, [f](std::optional<std::filesystem::path> p) {
        std::vector<std::filesystem::path> paths;
        if (p) {
            paths.push_back(*p);
        }
        send_paths(f, paths);
    });
    ELG_CATCH_VOID
}
/// Wraps `choose_folders()`: the system's folder dialog. Call it on the UI thread of a running
/// App; otherwise it records a UsageError.
///
/// @param directory Where the dialog opens; empty: the system's choice.
/// @param multiple Whether several folders can be chosen.
/// @param done Consumed closure, called once on the UI thread when the dialog closes; args:
///   `ElgPaths*`, the chosen folders (none if cancelled); ret: null. Required: none is a usage
///   error.
ELG_EXPORT void elg_choose_folders(ElgStr directory, bool multiple, ElgClosure done) {
    ELG_TRY
    const Consumed consumed(done);
    const SharedFn f = required(done, "done");
    choose_folders({path(directory), multiple}, [f](const std::vector<std::filesystem::path>& paths) { send_paths(f, paths); });
    ELG_CATCH_VOID
}

/// Wraps `notify()`: shows a system notification (Windows: a toast, also kept in the notification
/// center). Call it on the UI thread of a running App; otherwise it records a UsageError.
///
/// @param title The notification's title.
/// @param body Its text.
/// @param on_click Optional consumed closure (`call` null: none), run on the UI thread when the
///   notification is clicked; args and ret: null.
ELG_EXPORT void elg_notify(ElgStr title, ElgStr body, ElgClosure on_click) {
    ELG_TRY
    const Consumed consumed(on_click);
    notify({str(title), str(body), action(share(on_click))});
    ELG_CATCH_VOID
}

/// An elg::TrayMenuItem: a command, a check box, a submenu, or a separator (an empty title). The
/// function that takes the menu consumes its closures.
struct ElgTrayMenuItem {
    ElgStr title;                   ///< What the menu shows; empty: a separator.
    ElgClosure action;              ///< Runs when chosen (`call` null: none); args and ret: null.
    int32_t checked;                ///< -1: not a check box, 0/1: initially unchecked/checked
    /// Check boxes: runs when chosen, with the new state (`call` null: none); args: bool*, ret: null.
    ElgClosure toggled;
    bool enabled;                   ///< false: it can't be chosen.
    const ElgTrayMenuItem* submenu; ///< The submenu's items, read during the call.
    size_t submenu_count;           ///< Number of `submenu` items; 0: no submenu.
};

namespace {

// Consumed for a menu, a tree of items: take() takes the closures over in order (each item's
// action and toggled, then its submenu's), and on return or throw the destructor releases those it
// hasn't reached. Declare it first, like Consumed.
class ConsumedMenu {
public:
    ConsumedMenu(const ElgTrayMenuItem* items, size_t count) noexcept : items_(items), count_(count) {}
    ~ConsumedMenu() {
        size_t index = 0;
        release_untaken(items_, count_, index);
    }
    ConsumedMenu(const ConsumedMenu&) = delete;
    ConsumedMenu& operator=(const ConsumedMenu&) = delete;
    ConsumedMenu(ConsumedMenu&&) = delete;
    ConsumedMenu& operator=(ConsumedMenu&&) = delete;

    std::vector<TrayMenuItem> take() { return menu(items_, count_); }

private:
    std::vector<TrayMenuItem> menu(const ElgTrayMenuItem* items, size_t count) {
        std::vector<TrayMenuItem> result;
        result.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            TrayMenuItem item;
            item.title = str(items[i].title);
            item.action = action(next(items[i].action));
            if (items[i].checked >= 0) {
                item.checked = items[i].checked != 0;
            }
            item.toggled = consumer<bool>(next(items[i].toggled));
            item.enabled = items[i].enabled;
            item.submenu = menu(items[i].submenu, items[i].submenu_count);
            result.push_back(std::move(item));
        }
        return result;
    }
    // share() for the next closure in order.
    SharedFn next(ElgClosure closure) {
        SharedFn f = share(closure);
        ++taken_;
        return f;
    }
    void release_untaken(const ElgTrayMenuItem* items, size_t count, size_t& index) const noexcept {
        for (size_t i = 0; i < count; ++i) {
            for (ElgClosure closure : {items[i].action, items[i].toggled}) {
                if (index++ >= taken_) {
                    release(closure);
                }
            }
            release_untaken(items[i].submenu, items[i].submenu_count, index);
        }
    }

    const ElgTrayMenuItem* items_;
    size_t count_;
    size_t taken_ = 0;
};

} // namespace

/// Wraps `TrayIcon(TrayIconOptions)`: an icon in the system tray (Windows: the notification area)
/// with a menu, shown while any copy lives. Call it while an App runs; otherwise it records a
/// UsageError.
///
/// @param icon A file or data image (borrowed; must not be null); all fields empty: a plain dot in
///   the theme's accent color.
/// @param tooltip Shown when the pointer rests on the icon.
/// @param menu The menu's items, read during the call; their closures are consumed.
/// @param count Number of `menu` items.
/// @return The tray icon, owned by the caller (elg_tray_icon_free()), or nullptr on failure.
ELG_EXPORT TrayIcon* elg_tray_icon_new(const ElgImageSource* icon, ElgStr tooltip, const ElgTrayMenuItem* menu, size_t count) {
    ELG_TRY
    ConsumedMenu consumed(menu, count);
    return new TrayIcon({image_source(*icon), str(tooltip), consumed.take()});
    ELG_CATCH(nullptr)
}
/// Copies a tray icon handle; copies share the icon.
///
/// @param icon The tray icon (borrowed).
/// @return The copy, owned by the caller (elg_tray_icon_free()).
ELG_EXPORT TrayIcon* elg_tray_icon_clone(const TrayIcon* icon) {
    return new TrayIcon(*icon);
}
/// Frees a tray icon handle; the icon goes away with the last copy.
///
/// @param icon The tray icon (consumed); may be null.
ELG_EXPORT void elg_tray_icon_free(TrayIcon* icon) {
    delete icon;
}
/// Wraps `TrayIcon::set_tooltip()`.
///
/// @param icon The tray icon (borrowed).
/// @param tooltip The new tooltip.
ELG_EXPORT void elg_tray_icon_set_tooltip(const TrayIcon* icon, ElgStr tooltip) {
    ELG_TRY
    icon->set_tooltip(str(tooltip));
    ELG_CATCH_VOID
}
/// Wraps `TrayIcon::set_menu()`: replaces the menu.
///
/// @param icon The tray icon (borrowed).
/// @param menu The new menu's items, read during the call; their closures are consumed.
/// @param count Number of `menu` items.
ELG_EXPORT void elg_tray_icon_set_menu(const TrayIcon* icon, const ElgTrayMenuItem* menu, size_t count) {
    ELG_TRY
    ConsumedMenu consumed(menu, count);
    icon->set_menu(consumed.take());
    ELG_CATCH_VOID
}

// --- ABI layout (this file's structs and elg/gpu_image.h's) ---

/// Describes the layout of this file's structs and elg/gpu_image.h's, for elegance-sys's layout
/// test: one line per struct, "name size align".
///
/// @param out Receives each line.
/// @param ctx Passed to `out`.
ELG_EXPORT void elg_layouts_platform(ElgStrSink out, void* ctx) {
    ELG_LAYOUT(ElgDevice);
    ELG_LAYOUT(ElgRenderBuffer);
    ELG_LAYOUT(ElgSamples);
    ELG_LAYOUT(ElgFileFilter);
    ELG_LAYOUT(ElgPaths);
    ELG_LAYOUT(ElgTrayMenuItem);
    ELG_LAYOUT(ElgGpuImageFence);
    ELG_LAYOUT(ElgGpuImageRelease);
    ELG_LAYOUT(ElgGpuImageFrame);
}
