// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// New frames of videos, cameras, video feeds and GPU images: which windows draw them, and what
// wakes the render thread for them. No GPU: the render thread's bookkeeping, with stand-in
// sources.

#include "render/frame_sources.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
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
