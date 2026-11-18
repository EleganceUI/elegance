// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// A window whose surface is lost gets a new one, which its next frame is drawn on
// (ELGSPEC-0025.R1).

#include "gpu/device.h"
#include "gpu/instance.h"
#include "gpu/swapchain.h"
#include "platform/platform.h"

#include <elg/app.h>

#include <catch2/catch_test_macros.hpp>

#include <webgpu/webgpu_cpp.h>

#include <cstdint>

namespace {

using namespace elg::detail;

gpu::SurfaceSource gpu_surface(const platform::SurfaceSource& source) {
    using From = platform::SurfaceSource::Kind;
    using To = gpu::SurfaceSource::Kind;
    To kind = To::none;
    switch (source.kind) {
    case From::none: kind = To::none; break;
    case From::win32: kind = To::win32; break;
    case From::metal: kind = To::metal; break;
    case From::xlib: kind = To::xlib; break;
    case From::wayland: kind = To::wayland; break;
    }
    return {.kind = kind, .display = source.display, .handle = source.handle, .xlib_window = source.xlib_window};
}

// Clears the window's next image and presents it. Whether there was an image to draw on.
bool draw_frame(const gpu::Device& device, gpu::Swapchain& swapchain, std::uint32_t width, std::uint32_t height) {
    const wgpu::TextureView view = swapchain.begin_frame(width, height, {0, 0, 0, 1});
    if (!view) {
        return false;
    }
    wgpu::RenderPassColorAttachment color;
    color.view = view;
    color.loadOp = wgpu::LoadOp::Clear;
    color.storeOp = wgpu::StoreOp::Store;
    color.clearValue = {0.2, 0.4, 0.6, 1};
    wgpu::RenderPassDescriptor pass;
    pass.colorAttachmentCount = 1;
    pass.colorAttachments = &color;
    const wgpu::CommandEncoder encoder = device.handle().CreateCommandEncoder();
    encoder.BeginRenderPass(&pass).End();
    device.submit(encoder.Finish());
    swapchain.end_frame(false);
    return true;
}

} // namespace

TEST_CASE("a window whose surface was lost draws its next frame on a new one", "[.gpu]") {
    const std::unique_ptr<platform::Platform> platform = platform::create_platform();
    elg::WindowDesc desc;
    desc.title = "lost surface";
    desc.width = 64;
    desc.height = 64;
    const std::unique_ptr<platform::Window> window = platform::create_window(desc);
    const platform::PixelSize size = window->pixel_size();
    const auto width = static_cast<std::uint32_t>(size.width);
    const auto height = static_cast<std::uint32_t>(size.height);

    const gpu::Instance instance;
    const gpu::Device device(instance);
    gpu::Swapchain swapchain(device, gpu_surface(window->surface_source()));
    REQUIRE(draw_frame(device, swapchain, width, height));
    CHECK(swapchain.surfaces_made() == 1);

    swapchain.lose_surface_for_testing();
    CHECK(draw_frame(device, swapchain, width, height)); // this frame, on the new surface
    CHECK(swapchain.surfaces_made() == 2);
    CHECK(draw_frame(device, swapchain, width, height));
    CHECK(swapchain.surfaces_made() == 2);
}
