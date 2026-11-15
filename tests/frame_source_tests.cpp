// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// New frames of videos, cameras, video feeds and GPU images: which windows draw them, and what
// wakes the render thread for them. No GPU: the render thread's bookkeeping, with stand-in
// sources.

#include "render/frame_sources.h"

#include <elg/elegance.h>
#include <elg/gpu_image.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

using namespace elg;

namespace {

// A decoder's output: update() takes the frames decoded since it last ran, as decoders' do.
class FakeSource final : public detail::VideoSource {
public:
    void play() override { playing_now_ = true; }
    void pause() override { playing_now_ = false; }
    void seek(double) override {}
    void set_volume(float) override {}
    void set_muted(bool) override {}
    void set_looping(bool) override {}
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

detail::WindowFrame frame_of(std::uint32_t window_id, const std::vector<std::shared_ptr<detail::VideoSource>>& shown) {
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
        sources.drew(frame_of(1, {gpu_image_source(image)}));
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
    sources.drew(frame_of(1, {feed.source()}));
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
