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

// Platforms without a video backend yet: an error, shown by the view.
class UnsupportedVideoSource final : public VideoSource, public FrameSource {
public:
    explicit UnsupportedVideoSource(std::string message) { error = std::move(message); }
    std::shared_ptr<FrameSource> frames() override { return {shared_from_this(), this}; }
    void play() override {}
    void pause() override {}
    void seek(double) override {}
    void set_volume(float v) override { volume = v; }
    void set_muted(bool m) override { muted = m; }
    void set_looping(bool l) override { looping = l; }
    bool update() override { return false; }
    std::optional<VideoFrame> current() const override { return std::nullopt; }
};

// A video the OS's media engine plays (media/video_backend.h).
class EngineVideoSource final : public VideoSource, public FrameSource {
public:
    explicit EngineVideoSource(std::unique_ptr<VideoEngine> engine)
        : FrameSource(engine->wakes_for_frames()), engine_(std::move(engine)) {}
    std::shared_ptr<FrameSource> frames() override { return {shared_from_this(), this}; }

    // UI thread, right after construction: the engine holds the host weakly.
    void open() { engine_->open(std::shared_ptr<FrameSourceHost>(shared_from_this(), &host_)); }

    void play() override { engine_->play(); }
    void pause() override { engine_->pause(); }
    void seek(double seconds) override { engine_->seek(seconds); }
    void set_volume(float value) override { engine_->set_volume(value); }
    void set_muted(bool value) override { engine_->set_muted(value); }
    void set_looping(bool value) override { engine_->set_looping(value); }
    bool update() override { return engine_->update(); }
    std::optional<VideoFrame> current() const override { return engine_->current(); }

private:
    SourceHost host_{*this, *this}; // outlives the engine
    std::unique_ptr<VideoEngine> engine_;
};

} // namespace

// --- SourceHost ---

std::uint64_t SourceHost::source_id() const noexcept {
    return frames_.id;
}

std::uint64_t SourceHost::new_id() const {
    return next_media_id();
}

void SourceHost::set_playing_now(bool playing) noexcept {
    frames_.set_playing_now(playing);
}

bool SourceHost::is_playing_now() const noexcept {
    return frames_.is_playing_now();
}

void SourceHost::notify_new_frame() const {
    frames_.notify_new_frame();
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

std::shared_ptr<const DecodedImage> SourceHost::decode_image(std::span<const std::uint8_t> data) const {
    return detail::decode_image(data);
}

void SourceHost::set_playing(bool playing) {
    source_.playing = playing;
}

bool SourceHost::playing() const {
    return source_.playing.peek();
}

void SourceHost::set_ready() {
    source_.ready = true;
}

void SourceHost::set_time(double seconds) {
    source_.time = seconds;
}

void SourceHost::set_duration(double seconds) {
    source_.duration = seconds;
}

double SourceHost::duration() const {
    return source_.duration.peek();
}

void SourceHost::set_size(float width, float height) {
    source_.size = Size{width, height};
}

void SourceHost::set_volume(float volume) {
    source_.volume = volume;
}

void SourceHost::set_muted(bool muted) {
    source_.muted = muted;
}

void SourceHost::set_looping(bool looping) {
    source_.looping = looping;
}

bool SourceHost::looping() const {
    return source_.looping.peek();
}

void SourceHost::set_error(std::string message) {
    source_.error = std::move(message);
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
