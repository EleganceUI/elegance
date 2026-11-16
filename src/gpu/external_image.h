// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_GPU_EXTERNAL_IMAGE_H
#define ELG_GPU_EXTERNAL_IMAGE_H

#include "gpu/webgpu_types.h"

namespace elg::detail::gpu {

// A frame another API made (elg/gpu_image.h), imported into the renderer's device with its access
// begun: what VideoFrame::external names. It owns one immutable imported allocation and its open
// access; once the last reference goes, the access ends and the frame goes back to its producer.
class ExternalImage {
public:
    ExternalImage(const ExternalImage&) = delete;
    ExternalImage& operator=(const ExternalImage&) = delete;
    ExternalImage(ExternalImage&&) = delete;
    ExternalImage& operator=(ExternalImage&&) = delete;
    virtual ~ExternalImage() = default;

    // The device it was imported into, its texture, and the prevalidated sRGB view to sample.
    [[nodiscard]] virtual const wgpu::Device& device() const noexcept = 0;
    [[nodiscard]] virtual const wgpu::Texture& texture() const noexcept = 0;
    [[nodiscard]] virtual const wgpu::TextureView& view() const noexcept = 0;

protected:
    ExternalImage() = default;
};

} // namespace elg::detail::gpu

#endif // ELG_GPU_EXTERNAL_IMAGE_H
