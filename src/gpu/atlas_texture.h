// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_GPU_ATLAS_TEXTURE_H
#define ELG_GPU_ATLAS_TEXTURE_H

#include "gpu/webgpu_types.h"

#include <cstdint>
#include <memory>
#include <span>

namespace elg::detail::gpu {

class Device;

// The glyph atlas on the GPU: an RGBA8 MTSDF texture plus the bind group the UI shader samples
// it through. Filled from GlyphUploads produced on the UI thread.
class AtlasTexture {
public:
    // A square RGBA8 texture `side` pixels wide.
    AtlasTexture(const Device& device, const wgpu::BindGroupLayout& image_layout, std::uint32_t side);
    ~AtlasTexture();

    AtlasTexture(const AtlasTexture&) = delete;
    AtlasTexture& operator=(const AtlasTexture&) = delete;
    AtlasTexture(AtlasTexture&&) = delete;
    AtlasTexture& operator=(AtlasTexture&&) = delete;

    // Copies new glyphs into the texture. Queue writes are ordered with frames: frames submitted
    // earlier still see the old contents, later ones the new.
    // `rgba` is width * height * 4 bytes, top row first, for the rectangle at (x, y).
    void upload(std::uint32_t x, std::uint32_t y, std::uint32_t width, std::uint32_t height,
                std::span<const std::uint8_t> rgba);

    [[nodiscard]] const wgpu::BindGroup& group() const noexcept;

private:
    struct Objects; // the texture (starts cleared: WebGPU zero-initializes) and its group

    const Device& device_;
    std::unique_ptr<Objects> objects_;
};

} // namespace elg::detail::gpu

#endif // ELG_GPU_ATLAS_TEXTURE_H
