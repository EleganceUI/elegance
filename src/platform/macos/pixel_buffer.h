// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_PLATFORM_MACOS_PIXEL_BUFFER_H
#define ELG_PLATFORM_MACOS_PIXEL_BUFFER_H

// Core Video pixel buffers (decoded video, camera frames) as renderer frames.

#include "media/video_source.h"

#include <CoreVideo/CoreVideo.h>

#include <optional>

namespace elg::detail::macos {

// The pixel format decoders and cameras are asked for: NV12, as the renderer's YUV pass takes it.
inline constexpr OSType kPreferredPixelFormat = kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange;

// Copies an NV12 (video or full range) or BGRA pixel buffer into a packed `cpu` frame (no row
// padding) of `source_id`. nullopt for other formats.
std::optional<VideoFrame> pack_pixel_buffer(CVPixelBufferRef buffer, std::uint64_t source_id);

} // namespace elg::detail::macos

#endif // ELG_PLATFORM_MACOS_PIXEL_BUFFER_H
