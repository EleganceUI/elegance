// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_PLATFORM_MACOS_SURFACE_PUBLISHER_H
#define ELG_PLATFORM_MACOS_SURFACE_PUBLISHER_H

// Zero-copy video frames on macOS: IOSurface-backed BGRA pixel buffers imported into the
// renderer's Metal device as they are (elg/gpu_image.h's IOSurface transport), rather than
// copied into memory and uploaded again. Each buffer stays retained until the renderer is done
// with it, including the GPU's reads, so the decoder's pool can't reuse it early.

#include "media/video_source.h"

#include <CoreVideo/CoreVideo.h>

#include <memory>
#include <optional>

struct ElgGpuImage;

namespace elg::detail::macos {

class SurfacePublisher {
public:
    // Whether the renderer can import IOSurfaces now (a running Metal device with IOSurface
    // and MTLSharedEvent sharing). If not, decode to NV12 and copy (pixel_buffer.h).
    static bool available();
    // The pixel buffer attributes to ask decoders for: BGRA, IOSurface-backed, Metal-compatible.
    static CFDictionaryRef buffer_attributes();

    SurfacePublisher();
    ~SurfacePublisher();
    SurfacePublisher(const SurfacePublisher&) = delete;
    SurfacePublisher& operator=(const SurfacePublisher&) = delete;

    // Render thread. Hands `buffer` to the renderer (retaining it); false if it couldn't be
    // imported, and then nothing is retained.
    bool publish(CVPixelBufferRef buffer);
    // Render thread, as VideoSource's: the newest published frame.
    bool update();
    std::optional<VideoFrame> current() const;

private:
    ElgGpuImage* image_ = nullptr;
    std::shared_ptr<VideoSource> source_;
};

} // namespace elg::detail::macos

#endif // ELG_PLATFORM_MACOS_SURFACE_PUBLISHER_H
