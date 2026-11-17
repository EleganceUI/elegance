// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_MEDIA_MEDIA_ACCESS_H
#define ELG_MEDIA_MEDIA_ACCESS_H

// The frame sources behind the public media types, which keep them private (ELGSPEC-0001.R7):
// for the views that draw them, and for tests.

#include <elg/capture.h>
#include <elg/video.h>

#include <memory>

namespace elg::detail {

struct MediaAccess {
    static const std::shared_ptr<VideoSource>& source(const VideoPlayer& player) noexcept { return player.source_; }
    static const std::shared_ptr<VideoSource>& source(const Camera& camera) noexcept { return camera.source_; }
    static const std::shared_ptr<VideoSource>& source(const VideoFeed& feed) noexcept { return feed.source_; }
};

} // namespace elg::detail

#endif // ELG_MEDIA_MEDIA_ACCESS_H
