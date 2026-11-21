// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// What views do over Net and Media as the Mikros build has them (the stand-ins for downloads, audio, video
// and cameras): the whole library but for the real Net and Media, which these tests link in their place
// (ELGSPEC-0040.R21). An AsyncImage of a URL goes to its failure phase with the message, and a VideoPlayer
// says why it can't play and fails to.

#include "core/ui_tasks.h"
#include "view/view_node.h"

#include <elg/elegance.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <string>

using namespace elg;
using Catch::Matchers::ContainsSubstring;

namespace {

// Shows an AsyncImage of `url` and runs the UI thread's tasks once, for the phase it ends in.
AsyncImagePhase show_async_image(const std::string& url, AsyncImageOptions options) {
    detail::Scope scope;
    const detail::Scope::Use use(scope);
    AsyncImagePhase last;
    const View view = AsyncImage(
        url,
        [&](const AsyncImagePhase& phase) {
            last = phase;
            return View();
        },
        options);
    const std::unique_ptr<detail::Element> root = detail::lower(view, {});
    detail::run_ui_tasks();
    detail::flush_effects();
    return last;
}

template <typename Action>
std::string platform_error_of(const Action& action) {
    try {
        action();
    } catch (const PlatformError& error) {
        return error.what();
    }
    return {};
}

} // namespace

TEST_CASE("on Mikros, an AsyncImage of a URL goes to its failure phase, with or without the disk cache", "[mikros]") {
    for (const bool disk_cache : {false, true}) {
        const AsyncImagePhase phase = show_async_image("https://example.com/a.png", {.disk_cache = disk_cache});
        CHECK(phase.kind == AsyncImagePhase::Kind::failure);
        CHECK_THAT(phase.error, ContainsSubstring("Mikros doesn't offer networking"));
    }
}

TEST_CASE("on Mikros, a VideoPlayer says why it can't play, and playing it fails", "[mikros]") {
    const VideoPlayer player("clip.mp4");
    CHECK_THAT(player.error(), ContainsSubstring("Mikros doesn't offer video playback"));
    CHECK_THAT(platform_error_of([&] { player.play(); }), ContainsSubstring("video playback"));
    CHECK_THAT(platform_error_of([&] { player.toggle(); }), ContainsSubstring("video playback"));
    CHECK_FALSE(player.is_playing());
    CHECK_NOTHROW(player.pause());
    CHECK_NOTHROW(player.stop());
}
