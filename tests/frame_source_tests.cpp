// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// New frames of videos, cameras, video feeds and GPU images: which windows draw them, and what
// wakes the render thread for them. No GPU: the render thread's bookkeeping, with stand-in
// sources.

#include "core/ui_tasks.h"
#include "media/media_access.h"
#include "media/video_source.h"
#include "render/frame_sources.h"

#include <elg/elegance.h>
#include <elg/gpu_image.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace elg;

namespace {

// A decoder's output: update() takes the frames decoded since it last ran, as decoders' do.
class FakeSource final : public detail::FrameSource {
public:
    void play() { set_playing_now(true); }
    void pause() { set_playing_now(false); }
    bool update() override {
        ++updates;
        if (decoded_ == taken_) {
            return false;
        }
        taken_ = decoded_;
        return true;
    }
    std::optional<detail::VideoFrame> current() const override {
        detail::VideoFrame frame;
        frame.width = 16;
        frame.height = 16;
        frame.frame_id = taken_;
        return frame;
    }

    void decode() { ++decoded_; }
    int updates = 0;

private:
    std::uint64_t decoded_ = 0;
    std::uint64_t taken_ = 0;
};

detail::WindowFrame frame_of(std::uint32_t window_id, const std::vector<std::shared_ptr<detail::FrameSource>>& shown) {
    detail::WindowFrame frame;
    frame.window_id = window_id;
    for (const auto& source : shown) {
        frame.list.commands.emplace_back(detail::VideoCommand{{0, 0, 16, 16}, {0, 0, 1, 1}, source, 1, std::nullopt});
    }
    return frame;
}

std::vector<std::uint32_t> sorted(std::vector<std::uint32_t> ids) {
    std::ranges::sort(ids);
    return ids;
}

} // namespace

TEST_CASE("a new frame of a source shown in two windows is drawn in both", "[video][frames]") {
    detail::FrameSources sources;
    const auto video = std::make_shared<FakeSource>();
    const std::vector<detail::WindowFrame> frames{frame_of(1, {video}), frame_of(2, {video})};
    sources.update(frames);
    sources.drew(frames[0]);
    sources.drew(frames[1]);
    CHECK(sources.stale().empty());

    // One new frame: both windows draw it, though the source reports it only once.
    video->decode();
    video->updates = 0;
    sources.update();
    CHECK(video->updates == 1);
    CHECK(sorted(sources.stale()) == std::vector<std::uint32_t>{1, 2});
    REQUIRE(sources.frame(2));
    sources.redrew(1);
    CHECK(sources.stale() == std::vector<std::uint32_t>{2}); // until each has drawn it
    sources.redrew(2);
    CHECK(sources.stale().empty());

    // No new frame: nothing to draw.
    sources.update();
    CHECK(sources.stale().empty());
}

TEST_CASE("a window drawn from a new UI frame doesn't draw its sources' frames again", "[video][frames]") {
    detail::FrameSources sources;
    const auto video = std::make_shared<FakeSource>();
    const auto other = std::make_shared<FakeSource>();
    const std::vector<detail::WindowFrame> first{frame_of(1, {video}), frame_of(2, {video, other})};
    sources.update(first);
    sources.drew(first[0]);
    sources.drew(first[1]);

    video->decode();
    other->decode();
    // A new frame for window 1 arrives in the same round: it shows the new frame already.
    const std::vector<detail::WindowFrame> next{frame_of(1, {video})};
    sources.update(next);
    CHECK(video->updates == 2); // once in each round
    sources.drew(next[0]);
    CHECK(sources.stale() == std::vector<std::uint32_t>{2});

    // A window that stops showing sources isn't kept.
    sources.drew(frame_of(2, {}));
    CHECK(sources.frame(2) == nullptr);
    CHECK(sources.stale().empty());
    sources.remove(1);
    CHECK(sources.frame(1) == nullptr);
}

TEST_CASE("the render thread looks for frames regularly only while a shown source plays", "[video][frames]") {
    detail::FrameSources sources;
    const auto video = std::make_shared<FakeSource>();
    sources.drew(frame_of(1, {video}));
    CHECK_FALSE(sources.polling()); // paused
    video->play();
    CHECK(sources.polling());
    video->pause();
    CHECK_FALSE(sources.polling());
    video->play();
    sources.remove(1);
    CHECK_FALSE(sources.polling()); // playing, but in no window
}

namespace {

// Counts what wakes the render thread while it lives.
class WakeCounter {
public:
    WakeCounter() {
        detail::set_frame_waker([this] { ++wakes_; });
    }
    WakeCounter(const WakeCounter&) = delete;
    WakeCounter& operator=(const WakeCounter&) = delete;
    WakeCounter(WakeCounter&&) = delete;
    WakeCounter& operator=(WakeCounter&&) = delete;
    ~WakeCounter() { detail::set_frame_waker({}); }
    [[nodiscard]] int wakes() const { return wakes_.load(); }

private:
    std::atomic<int> wakes_{0};
};

std::vector<std::uint8_t> bgra(std::uint32_t width, std::uint32_t height, std::uint8_t value) {
    return std::vector<std::uint8_t>(std::size_t{width} * height * 4, value);
}

} // namespace

TEST_CASE("an idle window with a GPU image doesn't wake the render thread", "[video][frames]") {
    const WakeCounter counter;
    ElgGpuImage* const image = elg_gpu_image_create(16, 16, ELG_GPU_IMAGE_TOP_LEFT);
    REQUIRE(image);
    {
        detail::FrameSources sources;
        sources.drew(frame_of(1, {detail::gpu_image_source(image)->frames()}));
        // Nothing published: no reason to look for frames, now or later.
        CHECK_FALSE(sources.polling());
        for (int i = 0; i < 100; ++i) {
            sources.update();
            CHECK(sources.stale().empty());
        }
        elg_gpu_image_clear(image); // nothing to clear
        CHECK(counter.wakes() == 0);
    }
    elg_gpu_image_destroy(image);
}

TEST_CASE("a video feed wakes the render thread for each frame while a window shows it", "[video][frames]") {
    const WakeCounter counter;
    const VideoFeed feed;
    feed.push(VideoFeed::Format::bgra, 2, 2, bgra(2, 2, 1));
    CHECK(counter.wakes() == 0); // no window shows it

    detail::FrameSources sources;
    sources.drew(frame_of(1, {detail::MediaAccess::source(feed)->frames()}));
    CHECK_FALSE(sources.polling()); // it wakes the render thread instead
    feed.push(VideoFeed::Format::bgra, 2, 2, bgra(2, 2, 2));
    CHECK(counter.wakes() == 1);
    sources.update();
    CHECK(sources.stale() == std::vector<std::uint32_t>{1});
    sources.redrew(1);
    feed.clear();
    CHECK(counter.wakes() == 2);
    feed.clear(); // nothing left to clear
    CHECK(counter.wakes() == 2);

    sources.remove(1);
    feed.push(VideoFeed::Format::bgra, 2, 2, bgra(2, 2, 3));
    CHECK(counter.wakes() == 2);
}

TEST_CASE("waking the render thread ends its wait for work, even before it waits", "[video][frames]") {
    detail::FrameMailbox mailbox;
    detail::RenderBatch batch;
    const auto start = std::chrono::steady_clock::now();
    mailbox.wake();
    // Without the wake this waits the full minute.
    CHECK(mailbox.take_for(batch, std::chrono::minutes(1)));
    CHECK(std::chrono::steady_clock::now() - start < std::chrono::seconds(30));
    CHECK(batch.frames.empty());
    CHECK(batch.commands.empty());
    // One wake ends one wait.
    CHECK(mailbox.take_for(batch, std::chrono::milliseconds(0)));
}

TEST_CASE("each push and clear of a video feed wakes the render thread once its result shows", "[video][frames]") {
    const VideoFeed feed;
    detail::FrameSources sources;
    sources.drew(frame_of(1, {detail::MediaAccess::source(feed)->frames()}));
    // What the render thread would find when woken: the width of the feed's frame.
    std::vector<std::optional<std::uint32_t>> found;
    detail::set_frame_waker([&] {
        const std::optional<detail::VideoFrame> frame = detail::MediaAccess::source(feed)->frames()->current();
        found.push_back(frame ? std::optional(frame->width) : std::nullopt);
    });
    feed.push(VideoFeed::Format::bgra, 2, 2, bgra(2, 2, 1));
    feed.push(VideoFeed::Format::bgra, 4, 2, bgra(4, 2, 1));
    feed.clear();
    feed.push(VideoFeed::Format::bgra, 3, 3, bgra(3, 3, 1));
    detail::set_frame_waker({});
    CHECK(found == std::vector<std::optional<std::uint32_t>>{2, 4, std::nullopt, 3});
}

TEST_CASE("after pushes and clears from several threads, a feed's windows and views agree", "[video][frames]") {
    // Many rounds: some interleavings are rare.
    for (int round = 0; round < 10; ++round) {
        const VideoFeed feed;
        detail::run_ui_tasks(); // from earlier tests
        detail::FrameSources sources;
        sources.drew(frame_of(1, {detail::MediaAccess::source(feed)->frames()}));
        const WakeCounter counter;
        {
            std::vector<std::jthread> threads;
            threads.reserve(4);
            for (std::uint32_t t = 0; t < 4; ++t) {
                threads.emplace_back([&feed, t] {
                    for (std::uint32_t i = 0; i < 300; ++i) {
                        if ((i + t) % 3 == 0) {
                            feed.clear();
                        } else {
                            const std::uint32_t width = 1 + t;
                            const std::uint32_t height = 1 + i % 5;
                            feed.push(VideoFeed::Format::bgra, width, height, bgra(width, height, 0));
                        }
                    }
                });
            }
        }
        // Every call has returned: the render thread was woken after the last, and finds its
        // result.
        REQUIRE(counter.wakes() > 0);
        sources.update();
        REQUIRE(sources.stale() == std::vector<std::uint32_t>{1});
        const std::optional<detail::VideoFrame> frame = detail::MediaAccess::source(feed)->frames()->current();
        // The views' size and flags come to the same result.
        detail::run_ui_tasks();
        REQUIRE(feed.has_frame() == frame.has_value());
        const Size size = feed.size();
        REQUIRE(size.width == (frame ? static_cast<float>(frame->width) : 0.f));
        REQUIRE(size.height == (frame ? static_cast<float>(frame->height) : 0.f));
    }
}

namespace {

// Runs `call` once, the first time the render thread is woken: inside another call to the feed,
// after it changed the frame and before it told the views. As if `call` came from another thread
// just then.
class CallWhenWoken {
public:
    explicit CallWhenWoken(std::function<void()> call) : call_(std::move(call)) {
        detail::set_frame_waker([this] {
            if (call_) {
                std::exchange(call_, nullptr)();
            }
        });
    }
    CallWhenWoken(const CallWhenWoken&) = delete;
    CallWhenWoken& operator=(const CallWhenWoken&) = delete;
    CallWhenWoken(CallWhenWoken&&) = delete;
    CallWhenWoken& operator=(CallWhenWoken&&) = delete;
    ~CallWhenWoken() { detail::set_frame_waker({}); }

private:
    std::function<void()> call_;
};

} // namespace

TEST_CASE("a clear that comes between a push and its views' update wins", "[video][frames]") {
    const VideoFeed feed;
    detail::run_ui_tasks(); // from earlier tests
    detail::FrameSources sources;
    sources.drew(frame_of(1, {detail::MediaAccess::source(feed)->frames()}));
    {
        const CallWhenWoken clear([&feed] { feed.clear(); });
        feed.push(VideoFeed::Format::bgra, 4, 2, bgra(4, 2, 1));
    }
    detail::run_ui_tasks();
    CHECK_FALSE(detail::MediaAccess::source(feed)->frames()->current());
    CHECK_FALSE(feed.has_frame());
    CHECK(feed.size().width == 0);
}

TEST_CASE("a push that comes between a clear and its views' update wins", "[video][frames]") {
    const VideoFeed feed;
    feed.push(VideoFeed::Format::bgra, 4, 2, bgra(4, 2, 1));
    detail::run_ui_tasks();
    detail::FrameSources sources;
    sources.drew(frame_of(1, {detail::MediaAccess::source(feed)->frames()}));
    {
        const CallWhenWoken push([&feed] { feed.push(VideoFeed::Format::bgra, 3, 3, bgra(3, 3, 1)); });
        feed.clear();
    }
    detail::run_ui_tasks();
    REQUIRE(detail::MediaAccess::source(feed)->frames()->current());
    CHECK(feed.has_frame());
    CHECK(feed.size().width == 3);
    CHECK(feed.size().height == 3);
}

TEST_CASE("sources shown only in hidden windows don't wake the render thread, and show their newest frame again",
          "[video][frames]") {
    const WakeCounter counter;
    const VideoFeed feed;
    const auto video = std::make_shared<FakeSource>();
    video->play(); // asked for frames regularly while a visible window shows it
    detail::FrameSources sources;
    sources.drew(frame_of(1, {detail::MediaAccess::source(feed)->frames()}));
    sources.drew(frame_of(2, {video, detail::MediaAccess::source(feed)->frames()}));
    CHECK(sources.polling());

    // Both windows hidden or minimized.
    sources.set_visible(1, false);
    sources.set_visible(2, false);
    CHECK_FALSE(sources.polling());
    feed.push(VideoFeed::Format::bgra, 2, 2, bgra(2, 2, 1));
    feed.push(VideoFeed::Format::bgra, 5, 2, bgra(5, 2, 1));
    CHECK(counter.wakes() == 0);
    video->decode();
    video->updates = 0;
    sources.update();
    CHECK(video->updates == 0);
    CHECK(sources.stale().empty());

    // Window 2 shown again: it draws the sources' newest frames, and they wake the render thread.
    sources.set_visible(2, true);
    CHECK(sources.polling());
    sources.update();
    CHECK(video->updates == 1);
    CHECK(sources.stale() == std::vector<std::uint32_t>{2});
    const std::optional<detail::VideoFrame> newest = detail::MediaAccess::source(feed)->frames()->current();
    CHECK(newest.value_or(detail::VideoFrame{}).width == 5);
    sources.redrew(2);
    feed.clear();
    CHECK(counter.wakes() == 1);

    // Window 1, still hidden, draws nothing; shown, it catches up too.
    sources.update();
    CHECK(sources.stale() == std::vector<std::uint32_t>{2});
    sources.redrew(2);
    sources.set_visible(1, true);
    sources.update();
    CHECK(sources.stale() == std::vector<std::uint32_t>{1});
}

#if defined(__linux__)
TEST_CASE("a playing Linux video wakes the render thread for its frames instead of being polled", "[video][frames]") {
    // A short clip, made with GStreamer's own tools: 20 frames at 50 per second.
    constexpr int kFrames = 20;
    std::random_device random;
    const std::filesystem::path clip =
        std::filesystem::temp_directory_path() / std::format("elg-clip-{:x}.ogg", random());
    const std::string command = std::format("gst-launch-1.0 -q videotestsrc num-buffers={} ! "
                                            "video/x-raw,width=32,height=24,framerate=50/1 ! theoraenc ! oggmux ! "
                                            "filesink location={} > /dev/null 2>&1",
                                            kFrames, clip.string());
    // NOLINTNEXTLINE(cert-env33-c,concurrency-mt-unsafe): a fixed command, to make the test's input
    if (std::system(command.c_str()) != 0) {
        SKIP("making a clip needs gst-launch-1.0 and GStreamer's base plugins");
    }
    const std::shared_ptr<detail::VideoSource> video = detail::VideoSource::create(clip.string());
    if (!video->error.peek().empty()) {
        std::filesystem::remove(clip);
        SKIP(video->error.peek());
    }

    // The render thread, as far as this source goes: woken, it takes the new frame.
    std::mutex mutex;
    std::condition_variable woken;
    int wakes = 0;
    detail::set_frame_waker([&] {
        {
            const std::lock_guard lock(mutex);
            ++wakes;
        }
        woken.notify_one();
    });
    detail::FrameSources sources;
    sources.drew(frame_of(1, {video->frames()}));
    int frames = 0;
    int handled = 0; // wakes
    // Waits (up to `limit`) for a wake, then draws if there's a new frame. False on timeout.
    const auto next = [&](std::chrono::milliseconds limit) {
        {
            std::unique_lock lock(mutex);
            if (!woken.wait_for(lock, limit, [&] { return wakes > handled; })) {
                return false;
            }
            handled = wakes;
        }
        sources.update();
        if (!sources.stale().empty()) {
            ++frames;
            sources.redrew(1);
        }
        return true;
    };

    // Paused, it shows its first frame, woken for it.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (frames == 0 && std::chrono::steady_clock::now() < deadline) {
        next(std::chrono::milliseconds(100));
    }
    CHECK(frames == 1);
    CHECK_FALSE(sources.polling());

    // Playing, it isn't polled: each frame wakes the render thread, and nothing else does.
    video->play();
    CHECK(video->frames()->is_playing_now());
    CHECK_FALSE(sources.polling());
    while (std::chrono::steady_clock::now() < deadline) {
        if (!next(std::chrono::milliseconds(50))) {
            detail::run_ui_tasks(); // the end of the clip
            if (!video->playing.peek()) {
                break;
            }
        }
    }
    detail::set_frame_waker({});
    CHECK_FALSE(video->playing.peek()); // it played to its end
    CHECK(frames > 1);
    CHECK(wakes <= kFrames + 1); // the paused first frame, and one per frame played
    std::filesystem::remove(clip);
}
#endif

#if defined(__linux__)
TEST_CASE("a playing Linux video without frames reports its time, and doesn't wake the render thread",
          "[video][frames]") {
    // A sound alone, about a second and a half, made with GStreamer's own tools.
    std::random_device random;
    const std::filesystem::path clip =
        std::filesystem::temp_directory_path() / std::format("elg-sound-{:x}.ogg", random());
    const std::string command = std::format("gst-launch-1.0 -q audiotestsrc num-buffers=64 samplesperbuffer=1024 ! "
                                            "vorbisenc ! oggmux ! filesink location={} > /dev/null 2>&1",
                                            clip.string());
    // NOLINTNEXTLINE(cert-env33-c,concurrency-mt-unsafe): a fixed command, to make the test's input
    if (std::system(command.c_str()) != 0) {
        SKIP("making a clip needs gst-launch-1.0 and GStreamer's base plugins");
    }

    // The UI thread, as far as this source goes: woken when a task is posted.
    std::mutex mutex;
    std::condition_variable posted;
    bool pending = false;
    detail::set_ui_task_waker([&] {
        {
            const std::lock_guard lock(mutex);
            pending = true;
        }
        posted.notify_one();
    });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    // Runs the UI tasks once some are posted (or 100 ms passed).
    const auto run_tasks = [&] {
        {
            std::unique_lock lock(mutex);
            posted.wait_for(lock, std::chrono::milliseconds(100), [&] { return pending; });
            pending = false;
        }
        detail::run_ui_tasks();
    };

    const WakeCounter counter;
    const std::shared_ptr<detail::VideoSource> video = detail::VideoSource::create(clip.string());
    detail::FrameSources sources;
    sources.drew(frame_of(1, {video->frames()})); // shown, so a frame would wake the render thread
    while (!video->ready.peek() && video->error.peek().empty() && std::chrono::steady_clock::now() < deadline) {
        run_tasks();
    }
    if (!video->error.peek().empty()) {
        detail::set_ui_task_waker(nullptr);
        std::filesystem::remove(clip);
        SKIP(video->error.peek()); // e.g. no audio output plugin
    }
    REQUIRE(video->ready.peek());
    const double duration = video->duration.peek();
    REQUIRE(duration > 1);

    // While it plays, its time moves on, without a frame and without the render thread.
    video->play();
    CHECK_FALSE(sources.polling());
    double reported = 0; // the last time reported before the end
    while (video->playing.peek() && std::chrono::steady_clock::now() < deadline) {
        run_tasks();
        if (video->playing.peek()) {
            reported = video->time.peek();
        }
    }
    detail::set_ui_task_waker(nullptr);
    CHECK_FALSE(video->playing.peek()); // it played to its end
    CHECK(reported > 0);
    CHECK(reported < duration);
    CHECK(counter.wakes() == 0);
    std::filesystem::remove(clip);
}
#endif
