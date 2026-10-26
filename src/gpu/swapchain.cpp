// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include "gpu/swapchain.h"

#include "core/log.h"

#include <elg/error.h>

#include <algorithm>
#include <format>
#include <initializer_list>
#include <span>

namespace elg::detail::gpu {

namespace {

// sRGB views let the shaders work in linear space; the hardware encodes on write. Surfaces
// rarely offer sRGB formats themselves (Direct3D 12's don't), so 8-bit UNORM ones are
// configured with an sRGB view format.
wgpu::TextureFormat choose_format(std::span<const wgpu::TextureFormat> formats) {
    for (const wgpu::TextureFormat preferred :
         {wgpu::TextureFormat::BGRA8UnormSrgb, wgpu::TextureFormat::RGBA8UnormSrgb, wgpu::TextureFormat::BGRA8Unorm,
          wgpu::TextureFormat::RGBA8Unorm}) {
        if (std::ranges::find(formats, preferred) != formats.end()) {
            return preferred;
        }
    }
    throw GpuError("the window surface offers no 8-bit color format");
}

// Transparent windows want the compositor to blend with our (premultiplied) alpha; opaque ones
// want it ignored. INHERIT leaves it to the platform (e.g. a window made transparent with the
// window system), which is the next best thing for transparency.
wgpu::CompositeAlphaMode choose_alpha(std::span<const wgpu::CompositeAlphaMode> supported, bool transparent) {
    const auto pick = [&](std::initializer_list<wgpu::CompositeAlphaMode> modes) {
        for (const wgpu::CompositeAlphaMode mode : modes) {
            if (std::ranges::find(supported, mode) != supported.end()) {
                return mode;
            }
        }
        return wgpu::CompositeAlphaMode::Auto;
    };
    if (transparent) {
        return pick({wgpu::CompositeAlphaMode::Premultiplied, wgpu::CompositeAlphaMode::Inherit,
                     wgpu::CompositeAlphaMode::Unpremultiplied, wgpu::CompositeAlphaMode::Opaque});
    }
    return pick({wgpu::CompositeAlphaMode::Opaque, wgpu::CompositeAlphaMode::Inherit,
                 wgpu::CompositeAlphaMode::Premultiplied, wgpu::CompositeAlphaMode::Unpremultiplied});
}

} // namespace

Swapchain::Swapchain(const Device& device, wgpu::Surface surface, bool transparent)
    : device_(device), surface_(std::move(surface)) {
    wgpu::SurfaceCapabilities caps;
    if (surface_.GetCapabilities(device.adapter(), &caps) != wgpu::Status::Success) {
        throw GpuError(std::format("the GPU \"{}\" can't present to the window", device.name()));
    }
    format_ = choose_format({caps.formats, caps.formatCount});
    view_format_ = srgb_variant(format_);
    alpha_ = choose_alpha({caps.alphaModes, caps.alphaModeCount}, transparent);
    can_copy_ = (caps.usages & wgpu::TextureUsage::CopySrc) == wgpu::TextureUsage::CopySrc;
    if (transparent) {
        log_debug("transparent surface: alpha mode {}", static_cast<unsigned>(alpha_));
    }
}

Swapchain::~Swapchain() {
    if (width_ != 0) {
        surface_.Unconfigure();
        // Dawn keeps the native swapchain until the surface goes, then defers destroying it until
        // the GPU is past it: possibly the device's destruction, after the window is gone when
        // external frames keep the device alive (on Wayland that touches a dead wl_surface).
        // Destroy it now, while the window still exists.
        surface_ = nullptr;
        try {
            device_.finish_deferred();
        } catch (...) {
        }
    }
}

bool Swapchain::prepare(std::uint32_t width, std::uint32_t height) {
    if (width == 0 || height == 0) {
        return false;
    }
    if (!stale_ && width == width_ && height == height_) {
        return true;
    }
    wgpu::SurfaceConfiguration config;
    config.device = device_.handle();
    config.format = format_;
    config.usage = wgpu::TextureUsage::RenderAttachment;
    if (wants_copy_) {
        config.usage |= wgpu::TextureUsage::CopySrc;
    }
    config.width = width;
    config.height = height;
    if (view_format_ != format_) {
        config.viewFormatCount = 1;
        config.viewFormats = &view_format_;
    }
    config.alphaMode = alpha_;
    config.presentMode = wgpu::PresentMode::Fifo; // always available; vsync, no tearing
    surface_.Configure(&config);
    width_ = width;
    height_ = height;
    stale_ = false;
    copyable_ = wants_copy_;
    return true;
}

void Swapchain::request_copy_source() {
    if (can_copy_ && !wants_copy_) {
        wants_copy_ = true;
        stale_ = true; // reconfigured by the next prepare()
    }
}

wgpu::TextureView Swapchain::acquire() {
    wgpu::SurfaceTexture texture;
    surface_.GetCurrentTexture(&texture);
    switch (texture.status) {
    case wgpu::SurfaceGetCurrentTextureStatus::SuccessOptimal: break;
    case wgpu::SurfaceGetCurrentTextureStatus::SuccessSuboptimal:
        stale_ = true; // still presentable; reconfigured next frame
        break;
    case wgpu::SurfaceGetCurrentTextureStatus::Timeout:
    case wgpu::SurfaceGetCurrentTextureStatus::Outdated:
    case wgpu::SurfaceGetCurrentTextureStatus::Lost:
        // Resizing, or the surface went away for a moment: reconfigure, skip this frame.
        stale_ = true;
        return nullptr;
    default: throw_gpu_error("get the window's next image");
    }
    texture_ = texture.texture;
    wgpu::TextureViewDescriptor view;
    view.format = view_format_;
    return texture.texture.CreateView(&view);
}

void Swapchain::present() {
    texture_ = nullptr;
    if (surface_.Present() != wgpu::Status::Success) {
        stale_ = true;
    }
}

} // namespace elg::detail::gpu
