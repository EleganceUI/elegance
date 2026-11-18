// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include "render_pixels.h"

#include "gpu/atlas_texture.h"
#include "gpu/blur_renderer.h"
#include "gpu/device.h"
#include "gpu/instance.h"
#include "gpu/path_renderer.h"
#include "gpu/presenter.h"
#include "gpu/quad_renderer.h"
#include "gpu/texture_cache.h"
#include "gpu/webgpu.h"
#include "render/frame_mailbox.h"
#include "render/video_textures.h"
#include "render/window_target.h"
#include "text/atlas.h"

#include <elg/error.h>

#include <webgpu/webgpu_cpp.h>

#include <cstring>
#include <utility>

namespace elg_tests {

using namespace elg::detail;

namespace {

constexpr wgpu::TextureFormat kFormat = wgpu::TextureFormat::RGBA8UnormSrgb;

// A window's images, offscreen: one texture, the size of the last frame.
class OffscreenPresenter final : public gpu::Presenter {
public:
    explicit OffscreenPresenter(const gpu::Device& device) : device_(device) {}

    wgpu::TextureView begin_frame(std::uint32_t width, std::uint32_t height,
                                  const std::array<float, 4>& /*background*/) override {
        if (!texture_ || width != width_ || height != height_) {
            wgpu::TextureDescriptor descriptor;
            descriptor.label = "test window";
            descriptor.size = {width, height, 1};
            descriptor.format = kFormat;
            descriptor.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::CopySrc |
                               wgpu::TextureUsage::TextureBinding;
            texture_ = device_.handle().CreateTexture(&descriptor);
            width_ = width;
            height_ = height;
        }
        return texture_.CreateView();
    }
    [[nodiscard]] wgpu::TextureFormat view_format() const noexcept override { return kFormat; }
    [[nodiscard]] std::uint32_t width() const noexcept override { return width_; }
    [[nodiscard]] std::uint32_t height() const noexcept override { return height_; }
    [[nodiscard]] wgpu::Texture copy_source() const override { return texture_; }
    void end_frame(bool /*reads_backdrops*/) override {}

    [[nodiscard]] const wgpu::Texture& texture() const noexcept { return texture_; }

private:
    const gpu::Device& device_;
    wgpu::Texture texture_;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
};

} // namespace

std::array<int, 4> Pixels::at(int x, int y) const {
    const auto at = [](int v) { return static_cast<std::size_t>(v); };
    const std::size_t i = (at(y) * at(width) + at(x)) * 4;
    return {rgba[i], rgba[i + 1], rgba[i + 2], rgba[i + 3]};
}

struct PixelRenderer::Objects {
    gpu::Instance instance;
    gpu::Device device{instance};
    wgpu::BindGroupLayout layout = gpu::create_image_layout(device.handle());
    gpu::AtlasTexture atlas{device, layout, kAtlasSize};
    gpu::QuadRenderer quads{device, layout};
    gpu::BlurRenderer blur{device, layout};
    gpu::PathRenderer paths{device};
    gpu::TextureCache textures{device, layout};
    VideoTextures videos{device, layout};
    RenderResources resources{quads, textures, videos, blur, paths, layout, atlas.group()};
    OffscreenPresenter* presenter = nullptr; // the target's
    std::unique_ptr<WindowTarget> target;
};

PixelRenderer::PixelRenderer() : objects_(std::make_unique<Objects>()) {
    auto presenter = std::make_unique<OffscreenPresenter>(objects_->device);
    objects_->presenter = presenter.get();
    objects_->target = std::make_unique<WindowTarget>(objects_->device, std::move(presenter));
}

PixelRenderer::~PixelRenderer() = default;

bool PixelRenderer::draws() const {
    return objects_->device.backend() != wgpu::BackendType::Null;
}

const wgpu::Device& PixelRenderer::device() const {
    return objects_->device.handle();
}

Pixels PixelRenderer::draw(const DisplayList& list, int width, int height, float scale) {
    WindowFrame frame;
    frame.window_id = 1;
    frame.pixel_width = width;
    frame.pixel_height = height;
    frame.scale = scale;
    frame.list = list;
    frame.list.scale = scale;
    objects_->target->render(frame, objects_->resources);

    const gpu::Device& device = objects_->device;
    const auto w = static_cast<std::uint32_t>(width);
    const auto h = static_cast<std::uint32_t>(height);
    const std::uint32_t row = (w * 4 + 255) / 256 * 256; // WebGPU copies rows of 256-byte multiples
    wgpu::BufferDescriptor descriptor;
    descriptor.size = static_cast<std::uint64_t>(row) * h;
    descriptor.usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::MapRead;
    const wgpu::Buffer buffer = device.handle().CreateBuffer(&descriptor);
    wgpu::TexelCopyTextureInfo source;
    source.texture = objects_->presenter->texture();
    wgpu::TexelCopyBufferInfo destination;
    destination.buffer = buffer;
    destination.layout.bytesPerRow = row;
    destination.layout.rowsPerImage = h;
    const wgpu::Extent3D size{w, h, 1};
    const wgpu::CommandEncoder encoder = device.handle().CreateCommandEncoder();
    encoder.CopyTextureToBuffer(&source, &destination, &size);
    device.submit(encoder.Finish());
    bool mapped = false;
    objects_->instance.wait(buffer.MapAsync(wgpu::MapMode::Read, 0, descriptor.size, wgpu::CallbackMode::WaitAnyOnly,
                                            [&](wgpu::MapAsyncStatus status, wgpu::StringView) {
                                                mapped = status == wgpu::MapAsyncStatus::Success;
                                            }));
    if (!mapped) {
        throw elg::GpuError("the test couldn't read the frame back");
    }
    const auto* bytes = static_cast<const std::uint8_t*>(buffer.GetConstMappedRange(0, descriptor.size));
    Pixels pixels{width, height, std::vector<std::uint8_t>(static_cast<std::size_t>(w) * h * 4)};
    for (std::uint32_t y = 0; y < h; ++y) {
        std::memcpy(pixels.rgba.data() + static_cast<std::size_t>(y) * w * 4, bytes + static_cast<std::size_t>(y) * row,
                    static_cast<std::size_t>(w) * 4);
    }
    buffer.Unmap();
    return pixels;
}

} // namespace elg_tests
