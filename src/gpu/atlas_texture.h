// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_GPU_ATLAS_TEXTURE_H
#define ELG_GPU_ATLAS_TEXTURE_H

#include "gpu/device.h"
#include "gpu/webgpu.h"
#include "text/atlas.h"

#include <span>

namespace elg::detail::gpu {

// The glyph atlas on the GPU: an RGBA8 MTSDF texture plus the bind group the UI shader samples
// it through. Filled from GlyphUploads produced on the UI thread.
class AtlasTexture {
public:
    AtlasTexture(const Device& device, const wgpu::BindGroupLayout& image_layout);

    AtlasTexture(const AtlasTexture&) = delete;
    AtlasTexture& operator=(const AtlasTexture&) = delete;

    // Copies new glyphs into the texture. Queue writes are ordered with frames: frames submitted
    // earlier still see the old contents, later ones the new.
    void upload(std::span<const GlyphUpload> uploads);

    const wgpu::BindGroup& group() const noexcept { return group_; }

private:
    const Device& device_;
    wgpu::Texture texture_; // starts cleared: WebGPU zero-initializes
    wgpu::BindGroup group_;
};

} // namespace elg::detail::gpu

#endif // ELG_GPU_ATLAS_TEXTURE_H
