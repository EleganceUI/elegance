// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Videos on Linux (design.md §13.3, media/video_backend.h): GStreamer. A playbin decodes (in
// hardware where the system's plugins can), plays the sound and keeps it in sync; its video goes
// to an appsink as NV12, which the render thread packs into `cpu` frames. Each frame the appsink
// gets wakes the render thread. GStreamer is the system's (found with pkg-config when building);
// without it, video isn't available.

#include "media/video_backend.h"

#if defined(ELG_HAVE_GSTREAMER)

#include "core/log.h"
#include "core/profile.h"
#include "core/ui_tasks.h"

#include <gst/app/gstappsink.h>
#include <gst/gst.h>
#include <gst/video/video.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <format>
#include <mutex>
#include <stop_token>
#include <thread>
#include <utility>

namespace elg::detail {

namespace {

bool start_gstreamer() {
    static const bool started = [] {
        GError* error = nullptr;
        if (!gst_init_check(nullptr, nullptr, &error)) {
            log_warning("video: GStreamer didn't start: {}", error ? error->message : "unknown error");
            g_clear_error(&error);
            return false;
        }
        return true;
    }();
    return started;
}

// A file path or URL as a URI playbin takes.
std::string uri_of(const std::string& url) {
    if (gst_uri_is_valid(url.c_str())) {
        return url;
    }
    GError* error = nullptr;
    gchar* uri = gst_filename_to_uri(url.c_str(), &error);
    g_clear_error(&error);
    std::string result = uri ? uri : "";
    g_free(uri);
    return result;
}

double seconds_of(gint64 nanoseconds) {
    return nanoseconds < 0 ? 0.0 : static_cast<double>(nanoseconds) / GST_SECOND;
}

class GStreamerEngine final : public VideoEngine {
public:
    explicit GStreamerEngine(std::string url) : url_(std::move(url)) {}
    GStreamerEngine(const GStreamerEngine&) = delete;
    GStreamerEngine& operator=(const GStreamerEngine&) = delete;
    GStreamerEngine(GStreamerEngine&&) = delete;
    GStreamerEngine& operator=(GStreamerEngine&&) = delete;

    ~GStreamerEngine() override {
        stop_reporting(); // before the playbin it asks goes
        if (playbin_) {
            // Stopping joins the streaming threads, so no bus handler or appsink callback runs
            // past this; both are removed before anything is torn down.
            gst_element_set_state(playbin_, GST_STATE_NULL);
            if (sink_) {
                GstAppSinkCallbacks none{};
                gst_app_sink_set_callbacks(GST_APP_SINK(sink_), &none, nullptr, nullptr);
            }
            if (GstBus* bus = gst_element_get_bus(playbin_)) {
                gst_bus_set_sync_handler(bus, nullptr, nullptr, nullptr);
                gst_object_unref(bus);
            }
            gst_object_unref(playbin_);
        }
        if (sink_) {
            gst_object_unref(sink_);
        }
    }

    // The appsink's callbacks wake the render thread for each frame: it doesn't poll.
    [[nodiscard]] bool wakes_for_frames() const noexcept override { return true; }

    void open(const std::shared_ptr<FrameSourceHost>& host) override {
        host_ = host.get();
        weak_host_ = host;
        if (!start_gstreamer()) {
            fail("video playback needs GStreamer, which didn't start");
            return;
        }
        const std::string uri = uri_of(url_);
        playbin_ = gst_element_factory_make("playbin", nullptr);
        GstElement* sink = gst_element_factory_make("appsink", nullptr);
        if (uri.empty() || !playbin_ || !sink) {
            if (sink) {
                gst_object_unref(sink);
            }
            fail(uri.empty() ? std::format("can't play {}: not a valid file path or URL", url_)
                             : std::string("GStreamer's playbin or appsink is missing (gst-plugins-base)"));
            return;
        }
        gst_object_ref_sink(playbin_);
        // NV12 for the renderer's YUV pass; playbin converts what the decoder makes.
        GstCaps* caps = gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING, "NV12", nullptr);
        g_object_set(sink, "caps", caps, "sync", TRUE, "max-buffers", 1, "drop", TRUE, nullptr);
        gst_caps_unref(caps);
        // A frame to show (the first while paused, then each while playing) wakes the render
        // thread, which takes it in update(). The callbacks get the engine itself: the destructor
        // stops the pipeline, and so the callbacks, first.
        GstAppSinkCallbacks callbacks{};
        callbacks.new_preroll = &on_preroll;
        callbacks.new_sample = &on_sample;
        gst_app_sink_set_callbacks(GST_APP_SINK(sink), &callbacks, this, nullptr);
        sink_ = GST_ELEMENT(gst_object_ref(sink));
        g_object_set(playbin_, "uri", uri.c_str(), "video-sink", sink, nullptr);

        // The bus handler gets the engine too: the destructor removes it once the pipeline stopped.
        GstBus* bus = gst_element_get_bus(playbin_);
        gst_bus_set_sync_handler(bus, &on_message, this, nullptr);
        gst_object_unref(bus);
        // Paused: it prerolls, so the duration and size are known before playing.
        if (gst_element_set_state(playbin_, GST_STATE_PAUSED) == GST_STATE_CHANGE_FAILURE) {
            fail(std::format("can't play {}", url_));
        }
    }

    // --- UI thread ---

    void play() override {
        if (!playbin_) {
            return;
        }
        if (ended_) {
            seek(0);
        }
        gst_element_set_state(playbin_, GST_STATE_PLAYING);
        host_->set_playing(true);
        host_->set_playing_now(true);
        start_reporting();
    }

    void pause() override {
        stop_reporting();
        if (playbin_) {
            gst_element_set_state(playbin_, GST_STATE_PAUSED);
        }
        host_->set_playing(false);
        host_->set_playing_now(false);
    }

    void seek(double seconds) override {
        seconds = std::max(0.0, seconds);
        ended_ = false;
        if (playbin_) {
            gst_element_seek_simple(playbin_, GST_FORMAT_TIME,
                                    static_cast<GstSeekFlags>(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE),
                                    static_cast<gint64>(seconds * GST_SECOND));
        }
        host_->set_time(seconds);
    }

    void set_volume(float value) override {
        value = std::clamp(value, 0.f, 1.f);
        if (playbin_) {
            g_object_set(playbin_, "volume", static_cast<gdouble>(value), nullptr);
        }
        host_->set_volume(value);
    }

    void set_muted(bool value) override {
        if (playbin_) {
            g_object_set(playbin_, "mute", static_cast<gboolean>(value), nullptr);
        }
        host_->set_muted(value);
    }

    void set_looping(bool value) override { host_->set_looping(value); }

    // --- Render thread ---

    bool update() override {
        if (!playbin_) {
            return false;
        }
        GstElement* sink = nullptr;
        g_object_get(playbin_, "video-sink", &sink, nullptr);
        if (!sink) {
            return false;
        }
        // Paused (before playing, or after a seek): the preroll, so the view shows that frame.
        GstSample* sample = preroll_.exchange(false) ? gst_app_sink_try_pull_preroll(GST_APP_SINK(sink), 0) : nullptr;
        if (!sample) {
            sample = gst_app_sink_try_pull_sample(GST_APP_SINK(sink), 0);
        }
        gst_object_unref(sink);
        if (!sample) {
            return false;
        }
        std::optional<VideoFrame> frame = pack(sample);
        gst_sample_unref(sample);
        if (!frame) {
            return false;
        }
        const bool resized =
            !current_size_ || current_size_->first != frame->width || current_size_->second != frame->height;
        if (resized) {
            current_size_ = std::pair(frame->width, frame->height);
            post([width = static_cast<float>(frame->width), height = static_cast<float>(frame->height)](
                     FrameSourceHost& host) { host.set_size(width, height); });
        }
        const std::lock_guard lock(mutex_);
        current_ = std::move(frame);
        return true;
    }

    std::optional<VideoFrame> current() const override {
        const std::lock_guard lock(mutex_);
        return current_;
    }

private:
    // Runs `action` with the host on the UI thread, unless the source is gone by then (the engine
    // lives as long as its source).
    template <typename Action>
    void post(Action action) const {
        post_ui_task([weak = weak_host_, action = std::move(action)] {
            if (const auto host = weak.lock()) {
                action(*host);
            }
        });
    }

    // Any thread: the error the view shows.
    void fail(std::string message) {
        host_->set_playing_now(false);
        log_warning("video: {}", message);
        post([message = std::move(message)](FrameSourceHost& host) {
            host.set_error(message);
            host.set_playing(false);
        });
    }

    // UI thread: while it plays, the playback position a few times a second, from a thread of its
    // own: the render thread only wakes for frames, which a file without video, or a stalled
    // stream, doesn't have. The thread ends when the video pauses, ends, fails or goes.
    void start_reporting() {
        stop_reporting();
        reporter_ = std::jthread([this](const std::stop_token& stop) {
            ELG_THREAD_NAME("Elegance video");
            std::mutex mutex;
            std::condition_variable_any wake; // only for the stop request
            std::unique_lock lock(mutex);
            for (;;) {
                wake.wait_for(lock, stop, std::chrono::milliseconds(250), [] { return false; });
                if (stop.stop_requested() || !host_->is_playing_now()) {
                    return;
                }
                report_time();
            }
        });
    }
    void stop_reporting() {
        if (reporter_.joinable()) {
            reporter_.request_stop();
            reporter_.join();
        }
    }

    // The reporting thread: the playback position, to the UI thread.
    void report_time() {
        gint64 position = 0;
        if (gst_element_query_position(playbin_, GST_FORMAT_TIME, &position)) {
            post([this, at = seconds_of(position)](FrameSourceHost& host) {
                if (!ended_) { // a report from before the end doesn't move the time back
                    host.set_time(at);
                }
            });
        }
    }

    // A streaming thread: the appsink has a new frame, which update() takes on the render
    // thread. These only set a flag and wake the render thread, which is safe from any thread.
    // A preroll (the frame shown while paused: the first, or after a seek) is pulled apart from
    // played frames: pulling a played frame while paused drops the preroll.
    static GstFlowReturn on_preroll(GstAppSink*, gpointer data) {
        auto* self = static_cast<GStreamerEngine*>(data);
        self->preroll_ = true;
        self->host_->notify_new_frame();
        return GST_FLOW_OK;
    }
    static GstFlowReturn on_sample(GstAppSink*, gpointer data) {
        static_cast<const GStreamerEngine*>(data)->host_->notify_new_frame();
        return GST_FLOW_OK;
    }

    // GStreamer's streaming threads: forwards what matters to the UI thread.
    // Called by GStreamer on its streaming threads: what handling throws goes to the UI thread.
    static GstBusSyncReply on_message(GstBus* bus, GstMessage* message, gpointer data) {
        run_foreign_callback([&] { handle_message(bus, message, data); });
        return GST_BUS_DROP;
    }

    static GstBusSyncReply handle_message(GstBus*, GstMessage* message, gpointer data) {
        auto* const source = static_cast<GStreamerEngine*>(data);
        const auto host = source->weak_host_.lock(); // and so the engine, while this runs
        if (!host) {
            return GST_BUS_DROP;
        }
        switch (GST_MESSAGE_TYPE(message)) {
        case GST_MESSAGE_ERROR: {
            GError* error = nullptr;
            gst_message_parse_error(message, &error, nullptr);
            source->fail(std::format("can't play {}: {}", source->url_, error ? error->message : "unknown error"));
            g_clear_error(&error);
            break;
        }
        case GST_MESSAGE_EOS: source->post([source](FrameSourceHost& h) { source->on_end(h); }); break;
        case GST_MESSAGE_ASYNC_DONE:
        case GST_MESSAGE_DURATION_CHANGED: {
            gint64 length = 0;
            if (gst_element_query_duration(source->playbin_, GST_FORMAT_TIME, &length)) {
                source->post([length = seconds_of(length)](FrameSourceHost& h) {
                    h.set_duration(length);
                    h.set_ready();
                });
            }
            break;
        }
        default: break;
        }
        return GST_BUS_DROP;
    }

    // UI thread: it played to its end.
    void on_end(FrameSourceHost& host) {
        if (host.looping()) {
            seek(0);
            gst_element_set_state(playbin_, GST_STATE_PLAYING);
            return;
        }
        stop_reporting();
        ended_ = true;
        host.set_playing(false);
        host.set_playing_now(false);
        host.set_time(host.duration());
    }

    // Copies an NV12 sample into a packed frame (no row padding).
    std::optional<VideoFrame> pack(GstSample* sample) const {
        GstVideoInfo info;
        GstCaps* caps = gst_sample_get_caps(sample);
        GstBuffer* buffer = gst_sample_get_buffer(sample);
        if (!caps || !buffer || !gst_video_info_from_caps(&info, caps) ||
            GST_VIDEO_INFO_FORMAT(&info) != GST_VIDEO_FORMAT_NV12) {
            return std::nullopt;
        }
        GstVideoFrame mapped;
        if (!gst_video_frame_map(&mapped, &info, buffer, GST_MAP_READ)) {
            return std::nullopt;
        }
        const auto width = static_cast<std::uint32_t>(GST_VIDEO_INFO_WIDTH(&info));
        const auto height = static_cast<std::uint32_t>(GST_VIDEO_INFO_HEIGHT(&info));
        auto pixels = std::make_shared<std::vector<std::uint8_t>>(packed_size(VideoFrame::Format::nv12, width, height));
        std::uint8_t* out = pixels->data();
        const std::size_t rows[2] = {height, (height + 1) / 2};
        const std::size_t row_bytes[2] = {width, 2 * static_cast<std::size_t>((width + 1) / 2)};
        for (guint plane = 0; plane < 2; ++plane) {
            const auto* in = static_cast<const std::uint8_t*>(GST_VIDEO_FRAME_PLANE_DATA(&mapped, plane));
            const auto stride = static_cast<std::size_t>(GST_VIDEO_FRAME_PLANE_STRIDE(&mapped, plane));
            for (std::size_t row = 0; row < rows[plane]; ++row) {
                std::memcpy(out + row * row_bytes[plane], in + row * stride, std::min(row_bytes[plane], stride));
            }
            out += rows[plane] * row_bytes[plane];
        }
        gst_video_frame_unmap(&mapped);

        VideoFrame frame;
        frame.kind = VideoFrame::Kind::cpu;
        frame.width = width;
        frame.height = height;
        frame.format = VideoFrame::Format::nv12;
        frame.full_range = info.colorimetry.range == GST_VIDEO_COLOR_RANGE_0_255;
        frame.matrix = info.colorimetry.matrix == GST_VIDEO_COLOR_MATRIX_BT601 ? VideoFrame::Matrix::bt601
                                                                                : VideoFrame::Matrix::bt709;
        frame.pixels = std::move(pixels);
        frame.source_id = host_->source_id();
        frame.frame_id = host_->new_id();
        return frame;
    }

    const std::string url_;
    GstElement* playbin_ = nullptr;
    GstElement* sink_ = nullptr; // playbin's video sink (a reference of our own)
    std::atomic<bool> preroll_{false}; // the appsink has a preroll that update() hasn't taken
    FrameSourceHost* host_ = nullptr; // set by open(); outlives the engine
    std::weak_ptr<FrameSourceHost> weak_host_; // for what's posted to the UI thread
    bool ended_ = false; // UI thread
    std::jthread reporter_; // the playback position, while it plays (UI thread)
    // Render thread: the current frame's size.
    std::optional<std::pair<std::uint32_t, std::uint32_t>> current_size_;
    mutable std::mutex mutex_;
    std::optional<VideoFrame> current_;
};

} // namespace

std::expected<std::unique_ptr<VideoEngine>, std::string> create_video_engine(const std::string& url) {
    return std::make_unique<GStreamerEngine>(url);
}

} // namespace elg::detail

#else

namespace elg::detail {

std::expected<std::unique_ptr<VideoEngine>, std::string> create_video_engine(const std::string& url) {
    (void)url;
    return std::unexpected("video playback needs GStreamer, which this build doesn't have");
}

} // namespace elg::detail

#endif
