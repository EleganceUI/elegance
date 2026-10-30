// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_MEDIA_CAMERA_SOURCE_H
#define ELG_MEDIA_CAMERA_SOURCE_H

#include <elg/capture.h>

#include "media/video_source.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace elg::detail {

// A camera (design.md §13.3), as each OS captures it (platform/<os>/camera.*). play() starts
// capturing and pause() stops; frames reach the renderer through update() and current().
class CameraSource : public VideoSource {
public:
    // UI thread. The newest frame as a PNG image (not mirrored), or nullopt before the first.
    virtual std::optional<std::vector<unsigned char>> snapshot() const = 0;
};

// The system's cameras, and one of them opened (stopped). UI thread.
std::vector<CameraDevice> platform_camera_devices();
std::shared_ptr<CameraSource> create_platform_camera(CameraOptions options);

// Converts a packed frame (VideoFrame::Format, as in VideoFrame::pixels) to opaque BGRA, BT.601 or
// BT.709 by `matrix`. For snapshots of cameras that deliver YUV.
std::vector<std::uint8_t> frame_to_bgra(VideoFrame::Format format, VideoFrame::Matrix matrix, bool full_range,
                                        std::span<const std::uint8_t> pixels, std::uint32_t width,
                                        std::uint32_t height);

} // namespace elg::detail

#endif // ELG_MEDIA_CAMERA_SOURCE_H
