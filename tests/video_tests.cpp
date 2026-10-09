// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// The video player's state (design.md §13.3). Decoding starts once a renderer exists, so without
// an App the player only records what it's asked to do.

#include <elg/elegence.h>

#include <catch2/catch_test_macros.hpp>

using namespace elg;

TEST_CASE("video players record requests before they load", "[video]") {
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

TEST_CASE("video views build", "[video]") {
    const VideoPlayer player("clip.mp4");
    CHECK_NOTHROW(VideoView(player).aspect_ratio(16.f / 9.f).clip_shape(RoundedRectangle(8)));
    CHECK_NOTHROW(VideoView(player, ContentMode::fill));
    CHECK_NOTHROW(VideoControls(player));
    // Progress with no duration yet is 0 and seeking with it is harmless.
    CHECK(player.progress_binding().get() == 0.f);
    CHECK_NOTHROW(player.progress_binding().set(0.5f));
}
