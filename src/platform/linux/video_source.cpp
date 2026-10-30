// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Video sources on Linux (design.md §13.3): GStreamer. A playbin decodes (in hardware where the
// system's plugins can), plays the sound and keeps it in sync; its video goes to an appsink as
// NV12, which the render thread packs into `cpu` frames. GStreamer is the system's (found with
// pkg-config when building); without it, video isn't available.

#include "media/video_source.h"

#if defined(ELG_HAVE_GSTREAMER)

#include "core/log.h"
#include "core/ui_tasks.h"

#include <gst/app/gstappsink.h>
#include <gst/gst.h>
#include <gst/video/video.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <format>
#include <mutex>

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

class GStreamerSource final : public VideoSource {
public:
    explicit GStreamerSource(std::string url) : url_(std::move(url)) {}

    ~GStreamerSource() override {
        if (playbin_) {
            // Stopping joins the streaming threads, so no bus handler runs past this.
            gst_element_set_state(playbin_, GST_STATE_NULL);
            if (GstBus* bus = gst_element_get_bus(playbin_)) {
                gst_bus_set_sync_handler(bus, nullptr, nullptr, nullptr);
                gst_object_unref(bus);
            }
            gst_object_unref(playbin_);
        }
        delete weak_self_;
    }

    // UI thread, right after construction (bus messages need shared_from_this()).
    void open() {
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
        g_object_set(playbin_, "uri", uri.c_str(), "video-sink", sink, nullptr);

        weak_self_ = new std::weak_ptr<GStreamerSource>(std::static_pointer_cast<GStreamerSource>(shared_from_this()));
        GstBus* bus = gst_element_get_bus(playbin_);
        gst_bus_set_sync_handler(bus, &on_message, weak_self_, nullptr);
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
        playing = true;
        playing_now_ = true;
    }

    void pause() override {
        if (playbin_) {
            gst_element_set_state(playbin_, GST_STATE_PAUSED);
        }
        playing = false;
        playing_now_ = false;
    }

    void seek(double seconds) override {
        seconds = std::max(0.0, seconds);
        ended_ = false;
        if (playbin_) {
            gst_element_seek_simple(playbin_, GST_FORMAT_TIME,
                                    static_cast<GstSeekFlags>(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE),
                                    static_cast<gint64>(seconds * GST_SECOND));
        }
        time = seconds;
    }

    void set_volume(float value) override {
        value = std::clamp(value, 0.f, 1.f);
        if (playbin_) {
            g_object_set(playbin_, "volume", static_cast<gdouble>(value), nullptr);
        }
        volume = value;
    }

    void set_muted(bool value) override {
        if (playbin_) {
            g_object_set(playbin_, "mute", static_cast<gboolean>(value), nullptr);
        }
        muted = value;
    }

    void set_looping(bool value) override { looping = value; }

    // --- Render thread ---

    bool update() override {
        if (!playbin_) {
            return false;
        }
        report_time();
        GstElement* sink = nullptr;
        g_object_get(playbin_, "video-sink", &sink, nullptr);
        if (!sink) {
            return false;
        }
        GstSample* sample = gst_app_sink_try_pull_sample(GST_APP_SINK(sink), 0);
        if (!sample && !current_size_) {
            // Paused before playing: the first frame, so the view isn't empty.
            sample = gst_app_sink_try_pull_preroll(GST_APP_SINK(sink), 0);
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
        const bool resized = !current_size_ || current_size_->width != static_cast<float>(frame->width) ||
                             current_size_->height != static_cast<float>(frame->height);
        if (resized) {
            current_size_ = Size{static_cast<float>(frame->width), static_cast<float>(frame->height)};
            post([size = *current_size_](GStreamerSource& source) { source.size = size; });
        }
        std::lock_guard lock(mutex_);
        current_ = std::move(frame);
        return true;
    }

    std::optional<VideoFrame> current() const override {
        std::lock_guard lock(mutex_);
        return current_;
    }

private:
    template <typename Action>
    void post(Action action) {
        if (!weak_self_) {
            return;
        }
        post_ui_task([weak = *weak_self_, action = std::move(action)] {
            if (const auto source = weak.lock()) {
                action(*source);
            }
        });
    }

    // Any thread: the error the view shows.
    void fail(std::string message) {
        playing_now_ = false;
        log_warning("video: {}", message);
        post_ui_task([weak = std::weak_ptr<GStreamerSource>(std::static_pointer_cast<GStreamerSource>(shared_from_this())),
                      message = std::move(message)] {
            if (const auto source = weak.lock()) {
                source->error = message;
                source->playing = false;
            }
        });
    }

    // Render thread: the playback position, a few times a second.
    void report_time() {
        const auto now = std::chrono::steady_clock::now();
        if (now - last_time_report_ < std::chrono::milliseconds(250)) {
            return;
        }
        last_time_report_ = now;
        gint64 position = 0;
        if (gst_element_query_position(playbin_, GST_FORMAT_TIME, &position)) {
            post([at = seconds_of(position)](GStreamerSource& source) { source.time = at; });
        }
    }

    // GStreamer's streaming threads: forwards what matters to the UI thread.
    static GstBusSyncReply on_message(GstBus*, GstMessage* message, gpointer data) {
        const auto source = static_cast<std::weak_ptr<GStreamerSource>*>(data)->lock();
        if (!source) {
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
        case GST_MESSAGE_EOS: source->post([](GStreamerSource& s) { s.on_end(); }); break;
        case GST_MESSAGE_ASYNC_DONE:
        case GST_MESSAGE_DURATION_CHANGED: {
            gint64 length = 0;
            if (gst_element_query_duration(source->playbin_, GST_FORMAT_TIME, &length)) {
                source->post([length = seconds_of(length)](GStreamerSource& s) {
                    s.duration = length;
                    s.ready = true;
                });
            }
            break;
        }
        default: break;
        }
        return GST_BUS_DROP;
    }

    // UI thread: it played to its end.
    void on_end() {
        if (looping.peek()) {
            seek(0);
            gst_element_set_state(playbin_, GST_STATE_PLAYING);
            return;
        }
        ended_ = true;
        playing = false;
        playing_now_ = false;
        time = duration.peek();
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
        frame.source_id = VideoSource::id;
        frame.frame_id = next_media_id();
        return frame;
    }

    const std::string url_;
    GstElement* playbin_ = nullptr;
    // The bus handler's reference (it outlives no source: the handler is removed first).
    std::weak_ptr<GStreamerSource>* weak_self_ = nullptr;
    bool ended_ = false; // UI thread
    // Render thread.
    std::optional<Size> current_size_;
    std::chrono::steady_clock::time_point last_time_report_{};
    mutable std::mutex mutex_;
    std::optional<VideoFrame> current_;
};

} // namespace

std::shared_ptr<VideoSource> create_platform_video_source(std::string url) {
    auto source = std::make_shared<GStreamerSource>(std::move(url));
    source->open();
    return source;
}

} // namespace elg::detail

#else

namespace elg::detail {

std::shared_ptr<VideoSource> create_platform_video_source(std::string url) {
    (void)url;
    return VideoSource::unsupported("video playback needs GStreamer, which this build doesn't have");
}

} // namespace elg::detail

#endif
