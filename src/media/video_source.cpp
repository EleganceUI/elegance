// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Video sources (design.md §13.3). Each OS decodes with its own media engine, in
// platform/<os>/video_source.*: Windows' Media Foundation, macOS' AVFoundation, and GStreamer on
// Linux.

#include "media/video_source.h"

#include "core/ui_tasks.h"
#include "media/image_decode.h"
#include "media/source_host.h"
#include "media/video_backend.h"

#include <atomic>
#include <mutex>
#include <utility>

namespace elg::detail {

namespace {

std::mutex g_adapter_mutex;
std::optional<RenderAdapter> g_adapter;
NativeFrameImporter g_importer; // with g_adapter_mutex held

} // namespace

void set_render_adapter(RenderAdapter adapter) {
    const std::lock_guard lock(g_adapter_mutex);
    g_adapter = adapter;
}

std::optional<RenderAdapter> render_adapter() {
    const std::lock_guard lock(g_adapter_mutex);
    return g_adapter;
}

void set_native_frame_importer(NativeFrameImporter importer) {
    const std::lock_guard lock(g_adapter_mutex);
    g_importer = importer;
}

namespace {

NativeFrameImporter native_frame_importer() {
    const std::lock_guard lock(g_adapter_mutex);
    return g_importer;
}

} // namespace

namespace {

// The frames of a source that has none.
class NoFrames final : public FrameSource {
public:
    bool update() override { return false; }
    [[nodiscard]] std::optional<VideoFrame> current() const override { return std::nullopt; }
};

// Platforms without a video backend yet: an error, shown by the view.
class UnsupportedVideoSource final : public VideoSource {
public:
    explicit UnsupportedVideoSource(std::string message) { error = std::move(message); }
    std::shared_ptr<FrameSource> frames() override { return frames_; }
    void play() override {}
    void pause() override {}
    void seek(double) override {}
    void set_volume(float v) override { volume = v; }
    void set_muted(bool m) override { muted = m; }
    void set_looping(bool l) override { looping = l; }

private:
    const std::shared_ptr<NoFrames> frames_ = std::make_shared<NoFrames>();
};

// A video the OS's media engine plays (media/video_backend.h).
class EngineVideoSource final : public VideoSource {
public:
    explicit EngineVideoSource(std::unique_ptr<VideoEngine> engine)
        : frames_(std::make_shared<EngineFrames<VideoEngine>>(engine->wakes_for_frames())),
          box_(detail::make_ui_shared<EngineBox<VideoEngine>>(*this, frames_, std::move(engine))) {
        frames_->attach(box_);
    }
    EngineVideoSource(const EngineVideoSource&) = delete;
    EngineVideoSource& operator=(const EngineVideoSource&) = delete;
    EngineVideoSource(EngineVideoSource&&) = delete;
    EngineVideoSource& operator=(EngineVideoSource&&) = delete;
    // Its frames may outlive it in a display list: they get no more, and nothing the engine
    // reports reaches the states that go now.
    ~EngineVideoSource() override {
        box_->host.detach();
        frames_->set_playing_now(false);
    }

    // UI thread, right after construction.
    void open() { box_->engine->open(EngineBox<VideoEngine>::host_of(box_)); }

    std::shared_ptr<FrameSource> frames() override { return frames_; }
    void play() override { box_->engine->play(); }
    void pause() override { box_->engine->pause(); }
    void seek(double seconds) override { box_->engine->seek(seconds); }
    void set_volume(float value) override { box_->engine->set_volume(value); }
    void set_muted(bool value) override { box_->engine->set_muted(value); }
    void set_looping(bool value) override { box_->engine->set_looping(value); }

private:
    const std::shared_ptr<EngineFrames<VideoEngine>> frames_;
    const std::shared_ptr<EngineBox<VideoEngine>> box_;
};

} // namespace

// --- SourceHost ---

std::uint64_t SourceHost::source_id() const noexcept {
    return frames_->id;
}

std::uint64_t SourceHost::new_id() const {
    return next_media_id();
}

void SourceHost::set_playing_now(bool playing) noexcept {
    frames_->set_playing_now(playing);
}

bool SourceHost::is_playing_now() const noexcept {
    return frames_->is_playing_now();
}

void SourceHost::notify_new_frame() const {
    frames_->notify_new_frame();
}

std::optional<RenderAdapter> SourceHost::render_adapter() const {
    return detail::render_adapter();
}

bool SourceHost::imports_native_frames() const {
    const NativeFrameImporter importer = native_frame_importer();
    return importer.imports_native_frames && importer.imports_native_frames();
}

std::optional<VideoFrame> SourceHost::import_native_frame(const VideoFrame& frame) {
    const NativeFrameImporter importer = native_frame_importer();
    return importer.import_native_frame ? importer.import_native_frame(frame) : std::nullopt;
}

bool SourceHost::imported_frame_current(const VideoFrame& frame) const {
    const NativeFrameImporter importer = native_frame_importer();
    return importer.imported_frame_current && importer.imported_frame_current(frame);
}

std::shared_ptr<const DecodedImage> SourceHost::decode_image(std::span<const std::uint8_t> data) const {
    return detail::decode_image(data);
}

void SourceHost::set_playing(bool playing) {
    if (source_) {
        source_->playing = playing;
    }
}

bool SourceHost::playing() const {
    return source_ && source_->playing.peek();
}

void SourceHost::set_ready() {
    if (source_) {
        source_->ready = true;
    }
}

void SourceHost::set_time(double seconds) {
    if (source_) {
        source_->time = seconds;
    }
}

void SourceHost::set_duration(double seconds) {
    if (source_) {
        source_->duration = seconds;
    }
}

double SourceHost::duration() const {
    return source_ ? source_->duration.peek() : 0.0;
}

void SourceHost::set_size(float width, float height) {
    if (source_) {
        source_->size = Size{width, height};
    }
}

void SourceHost::set_volume(float volume) {
    if (source_) {
        source_->volume = volume;
    }
}

void SourceHost::set_muted(bool muted) {
    if (source_) {
        source_->muted = muted;
    }
}

void SourceHost::set_looping(bool looping) {
    if (source_) {
        source_->looping = looping;
    }
}

bool SourceHost::looping() const {
    return source_ && source_->looping.peek();
}

void SourceHost::set_error(std::string message) {
    if (source_) {
        source_->error = std::move(message);
    }
}

// --- VideoSource ---

std::shared_ptr<VideoSource> VideoSource::create(const std::string& url) {
    std::expected<std::unique_ptr<VideoEngine>, std::string> engine = create_video_engine(url);
    if (!engine) {
        return unsupported(std::move(engine.error()));
    }
    auto source = detail::make_ui_shared<EngineVideoSource>(std::move(*engine));
    source->open();
    return source;
}

std::shared_ptr<VideoSource> VideoSource::unsupported(std::string message) {
    return detail::make_ui_shared<UnsupportedVideoSource>(std::move(message));
}

} // namespace elg::detail
