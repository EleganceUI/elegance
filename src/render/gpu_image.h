// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_RENDER_GPU_IMAGE_H
#define ELG_RENDER_GPU_IMAGE_H

// Frames made in GPU memory outside the renderer: GpuImage's (elg/gpu_image.h, gpu_image.cpp),
// and the system decoders' (VideoFrame::Kind::native), imported through the GPU layer.

#include "media/video_frame.h"

#include <optional>

namespace elg::detail {

// What the render thread gives Media (set_native_frame_importer()): whether the running renderer
// can import native frames now, and `frame` (Kind::native) imported into its device as a
// Kind::external frame, or nullopt if it can't be. Any thread.
bool imports_native_frames();
std::optional<VideoFrame> import_native_frame(const VideoFrame& frame);

} // namespace elg::detail

#endif // ELG_RENDER_GPU_IMAGE_H
