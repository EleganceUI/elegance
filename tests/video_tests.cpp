// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// The video player's state (design.md §13.3). Decoding starts once a renderer exists, so without
// an App the player only records what it's asked to do.

#include <elg/elegance.h>

#include "render/frame_plan.h"
#include "view/view_node.h"

#include <catch2/catch_test_macros.hpp>

using namespace elg;

namespace {

class ResizingVideoSource final : public detail::VideoSource {
public:
    void play() override {}
    void pause() override {}
    void seek(double) override {}
    void set_volume(float) override {}
    void set_muted(bool) override {}
    void set_looping(bool) override {}
    bool update() override {
        ++updates;
        return true;
    }
    std::optional<detail::VideoFrame> current() const override {
        ++snapshots;
        return next;
    }

    int updates = 0;
    mutable int snapshots = 0;
    std::optional<detail::VideoFrame> next;
};

} // namespace

TEST_CASE("video players record requests before they load", "[video]") {
#if !defined(_WIN32) && !defined(__APPLE__)
    SKIP("video playback isn't implemented on this platform yet");
#endif
    const VideoPlayer player("does-not-matter.mp4");
    CHECK_FALSE(player.is_playing());
    CHECK_FALSE(player.is_ready());
    CHECK(player.error().empty());

    player.play();
    CHECK(player.is_playing());
    player.set_volume(0.25f);
    CHECK(player.volume() == 0.25f);
    player.set_volume(3.f); // clamped
    CHECK(player.volume() == 1.f);
    player.set_muted(true);
    CHECK(player.is_muted());
    player.set_looping(true);
    CHECK(player.is_looping());
    player.seek(4);
    CHECK(player.time() == 4.0);
    player.stop();
    CHECK_FALSE(player.is_playing());
    CHECK(player.time() == 0.0);

    // Copies control the same video.
    const VideoPlayer copy = player;
    copy.play();
    CHECK(player.is_playing());
}

TEST_CASE("video geometry follows the sampled snapshot without UI size updates",
                    "[video][render]") {
    auto source = std::make_shared<ResizingVideoSource>();
    source->size = Size{1920, 1080}; // Deliberately stale throughout both plans.
    source->next.emplace();
    source->next->width = 100;
    source->next->height = 200;
    source->next->frame_id = 1;
    const auto lease = std::make_shared<int>(42);
    source->next->texture_lease = lease;

    detail::DisplayList list;
    list.scale = 2;
    list.commands.push_back(detail::VideoCommand{
            {10, 20, 200, 100}, {1, 1, -1, -1}, source, 0.5f, ContentMode::fit});
    // Repeated draws must share one snapshot, even if the producer changes
    // between reads.
    list.commands.push_back(list.commands.front());
    const auto portrait = detail::plan_frame(list, 1000, 1000);
    REQUIRE(portrait.instances.size() == 2);
    CHECK(portrait.instances[0].rect == std::array<float, 4>{170, 40, 100, 200});
    CHECK(portrait.instances[1].rect == portrait.instances[0].rect);
    CHECK(portrait.instances[0].shape == std::array<float, 4>{1, 1, 0, 0});
    CHECK(portrait.instances[0].color[3] == 0.5f);
    CHECK(source->updates == 1);
    CHECK(source->snapshots == 1);
    REQUIRE(portrait.video_frames.size() == 1);
    REQUIRE(portrait.video_frames[0]);
    CHECK(portrait.video_frames[0]->texture_lease == lease);

    source->next->width = 400;
    source->next->height = 100;
    source->next->frame_id = 2;
    const auto landscape = detail::plan_frame(list, 1000, 1000);
    REQUIRE(landscape.instances.size() == 2);
    CHECK(landscape.instances[0].rect == std::array<float, 4>{20, 90, 400, 100});
    CHECK(landscape.video_frames[0]->frame_id == 2);
    CHECK(portrait.video_frames[0]->frame_id == 1);
    CHECK(portrait.video_frames[0]->width == 100);
    CHECK(source->size.peek().width == 1920);
}

TEST_CASE("video fill keeps destination clipping through transforms",
                    "[video][render]") {
    auto source = std::make_shared<ResizingVideoSource>();
    source->next.emplace();
    source->next->width = 100;
    source->next->height = 200;
    detail::DisplayList list;
    list.commands.push_back(
            detail::PushClipCommand{{10, 20, 200, 100}, {8, 8, 8, 8}});
    list.commands.push_back(detail::VideoCommand{
            {10, 20, 200, 100}, {1, 0, -1, 1}, source, 1, ContentMode::fill});
    list.commands.push_back(detail::PopClipCommand{});
    detail::transform(list.commands, 5, 7, 2, {});
    const auto plan = detail::plan_frame(list, 1000, 1000);
    REQUIRE(plan.instances.size() == 1);
    CHECK(plan.instances[0].rect == std::array<float, 4>{25, -253, 400, 800});
    CHECK(plan.instances[0].clip_rect == std::array<float, 4>{25, 47, 400, 200});
    CHECK(plan.instances[0].clip_radii == std::array<float, 4>{16, 16, 16, 16});
    CHECK(plan.instances[0].shape == std::array<float, 4>{1, 0, 0, 1});
}

TEST_CASE(
        "video planning handles missing frames and preserves explicit rectangles",
        "[video][render]") {
    auto source = std::make_shared<ResizingVideoSource>();
    detail::DisplayList list;
    list.commands.push_back(detail::VideoCommand{
            {10, 20, 200, 100}, {}, source, 1, ContentMode::fit});
    CHECK(detail::plan_frame(list, 500, 500).instances.empty());
    source->next.emplace();
    source->next->width = 100;
    CHECK(detail::plan_frame(list, 500, 500).instances.empty());
    source->next->height = 200;
    std::get<detail::VideoCommand>(list.commands[0]).content_mode.reset();
    const auto plan = detail::plan_frame(list, 500, 500);
    REQUIRE(plan.instances.size() == 1);
    CHECK(plan.instances[0].rect == std::array<float, 4>{10, 20, 200, 100});
}

TEST_CASE("video view defers picture geometry even before metadata arrives",
                    "[video][render]") {
    detail::Scope scope;
    detail::Scope::Use use(scope);
    const VideoFeed feed;
    auto root = detail::lower(VideoFeedView(feed, ContentMode::fill, true), {});
    root->place({10, 20}, {200, 100});
    detail::DisplayList list;
    root->paint(list);
    REQUIRE(list.commands.size() == 4);
    CHECK(std::holds_alternative<detail::PushClipCommand>(list.commands[1]));
    const auto &video = std::get<detail::VideoCommand>(list.commands[2]);
    CHECK(video.content_mode == ContentMode::fill);
    CHECK(video.rect.x == 10);
    CHECK(video.rect.y == 20);
    CHECK(video.rect.width == 200);
    CHECK(video.rect.height == 100);
    CHECK(video.uv.x == 1);
    CHECK(video.uv.width == -1);
    CHECK(std::holds_alternative<detail::PopClipCommand>(list.commands[3]));
}

TEST_CASE("video views build", "[video]") {
    const VideoPlayer player("clip.mp4");
    CHECK_NOTHROW(VideoView(player).aspect_ratio(16.f / 9.f).clip_shape(RoundedRectangle(8)));
    CHECK_NOTHROW(VideoView(player, ContentMode::fill));
    CHECK_NOTHROW(VideoControls(player));
    // Progress with no duration yet is 0 and seeking with it is harmless.
    CHECK(player.progress_binding().get() == 0.f);
    CHECK_NOTHROW(player.progress_binding().set(0.5f));
}
