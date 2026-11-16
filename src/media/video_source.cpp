// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Video sources (design.md §13.3). Each OS decodes with its own media engine, in
// platform/<os>/video_source.*: Windows' Media Foundation, macOS' AVFoundation, and GStreamer on
// Linux.

#include "media/video_source.h"

#include "core/ui_tasks.h"

#include <atomic>
#include <mutex>

namespace elg::detail {

namespace {

std::atomic<std::uint64_t> g_next_id{1};
std::mutex g_adapter_mutex;
std::optional<RenderAdapter> g_adapter;
// Called with the mutex held, so that it can't run once cleared. Recursive: what a waker does may
// wake it again.
std::recursive_mutex g_waker_mutex;
std::function<void()> g_waker;

} // namespace

void set_render_adapter(RenderAdapter adapter) {
    const std::lock_guard lock(g_adapter_mutex);
    g_adapter = adapter;
}

std::optional<RenderAdapter> render_adapter() {
    const std::lock_guard lock(g_adapter_mutex);
    return g_adapter;
}

std::uint64_t next_media_id() {
    return g_next_id.fetch_add(1);
}

void set_frame_waker(std::function<void()> waker) {
    const std::lock_guard lock(g_waker_mutex);
    g_waker = std::move(waker);
}

VideoSource::VideoSource(bool wakes_for_frames) : id(next_media_id()), wakes_for_frames_(wakes_for_frames) {}

void VideoSource::notify_new_frame() const {
    if (!shown_) {
        return; // the render thread updates it once a window shows it
    }
    const std::lock_guard lock(g_waker_mutex);
    if (g_waker) {
        g_waker();
    }
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

std::shared_ptr<VideoSource> VideoSource::create(const std::string& url) {
    return create_platform_video_source(url);
}

std::shared_ptr<VideoSource> VideoSource::unsupported(std::string message) {
    return detail::make_ui_shared<UnsupportedVideoSource>(std::move(message));
}

} // namespace elg::detail
