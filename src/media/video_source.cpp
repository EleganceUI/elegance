// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Video sources (design.md §13.3). Each OS decodes with its own media engine, in
// platform/<os>/video_source.*: Windows' Media Foundation, macOS' AVFoundation, and GStreamer on
// Linux.

#include "media/video_source.h"

#include <atomic>
#include <mutex>

namespace elg::detail {

namespace {

std::atomic<std::uint64_t> g_next_id{1};
std::mutex g_adapter_mutex;
std::optional<RenderAdapter> g_adapter;

} // namespace

void set_render_adapter(RenderAdapter adapter) {
    std::lock_guard lock(g_adapter_mutex);
    g_adapter = adapter;
}

std::optional<RenderAdapter> render_adapter() {
    std::lock_guard lock(g_adapter_mutex);
    return g_adapter;
}

std::uint64_t next_media_id() {
    return g_next_id.fetch_add(1);
}

VideoSource::VideoSource() : id(next_media_id()) {}

std::size_t packed_size(VideoFrame::Format format, std::uint32_t width, std::uint32_t height) {
    const std::size_t pixels = static_cast<std::size_t>(width) * height;
    switch (format) {
    case VideoFrame::Format::rgba:
    case VideoFrame::Format::bgra: return pixels * 4;
    case VideoFrame::Format::yuy2: return pixels * 2;
    case VideoFrame::Format::nv12:
    case VideoFrame::Format::i420: return pixels + 2 * (static_cast<std::size_t>((width + 1) / 2) * ((height + 1) / 2));
    }
    return 0;
}

namespace {

// Platforms without a video backend yet: an error, shown by the view.
class UnsupportedVideoSource final : public VideoSource {
public:
    explicit UnsupportedVideoSource(std::string message) { error = std::move(message); }
    void play() override {}
    void pause() override {}
    void seek(double) override {}
    void set_volume(float v) override { volume = v; }
    void set_muted(bool m) override { muted = m; }
    void set_looping(bool l) override { looping = l; }
    bool update() override { return false; }
    std::optional<VideoFrame> current() const override { return std::nullopt; }
};

} // namespace

std::shared_ptr<VideoSource> VideoSource::create(std::string url) {
    return create_platform_video_source(std::move(url));
}

std::shared_ptr<VideoSource> VideoSource::unsupported(std::string message) {
    return std::make_shared<UnsupportedVideoSource>(std::move(message));
}

} // namespace elg::detail
