// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#if defined(_WIN32)

#include "platform/windows/composition.h"

#include "core/log.h"
#include "platform/generic/platform.h"

#include <elg/error.h>

#include <windows.h> // the build defines WIN32_LEAN_AND_MEAN and NOMINMAX
#include <d3d11_1.h>
#include <dcomp.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#include <cstring>
#include <format>
#include <string>

namespace elg::detail::gpu {

using Microsoft::WRL::ComPtr;

namespace {

void check_hr(HRESULT result, std::string_view what) {
    if (FAILED(result)) {
        throw GpuError(std::format("failed to {} (HRESULT 0x{:08x})", what, static_cast<unsigned long>(result)));
    }
}

} // namespace

struct CompositionTarget::Impl {
    // D3D11 on the same adapter as the WebGPU device.
    ComPtr<ID3D11Device> d3d;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11Query> done; // signaled when the copy into the swapchain has executed
    ComPtr<IDXGISwapChain1> swapchain;
    HANDLE ready = nullptr; // the swapchain's frame-latency waitable object
    ComPtr<IDCompositionDevice> dcomp;
    ComPtr<IDCompositionTarget> target;
    ComPtr<IDCompositionVisual> root;
    ComPtr<IDCompositionVisual> background; // opaque windows: a 1x1 surface scaled over everything
    ComPtr<IDCompositionSurface> background_surface;
    ComPtr<IDCompositionVisual> visual; // the frames

    // The shared texture: created by D3D11, imported into WebGPU.
    ComPtr<ID3D11Texture2D> shared;
    wgpu::SharedTextureMemory memory;
    wgpu::Texture texture;
    wgpu::TextureView view;
    bool accessing = false;
};

bool CompositionTarget::supported(const Device& device) {
    return device.has_feature(wgpu::FeatureName::SharedTextureMemoryDXGISharedHandle) && device.luid().has_value();
}

bool composition_allowed() {
    static const bool allowed = [] {
        const std::string present = platform::environment_variable("ELG_PRESENT").value_or("");
        if (present == "swapchain") {
            log_info("windows present through WebGPU surfaces (ELG_PRESENT=swapchain)");
            return false;
        }
        // RenderDoc's capture library is injected when the process starts, before any window.
        if (GetModuleHandleA("renderdoc.dll") != nullptr) {
            log_info("windows present through WebGPU surfaces (RenderDoc is attached, and it can't capture "
                     "DirectComposition)");
            return false;
        }
        if (present == "surface") {
            log_info("opaque windows present through WebGPU surfaces, transparent ones through "
                     "DirectComposition (ELG_PRESENT=surface)");
        } else {
            if (!present.empty()) {
                log_warning("ELG_PRESENT={} is not recognized (use swapchain or surface)", present);
            }
            log_info("windows present through DirectComposition");
        }
        return true;
    }();
    return allowed;
}

CompositionTarget::CompositionTarget(const Device& device, void* hwnd, bool transparent)
    : device_(device), impl_(std::make_unique<Impl>()), transparent_(transparent) {
    if (!supported(device)) {
        throw GpuError(std::format("the GPU backend ({}) can't share images with Direct3D 11",
                                   to_string(device.backend())));
    }

    // The WebGPU device's adapter, by LUID.
    LUID luid;
    static_assert(sizeof(luid) == 8);
    std::memcpy(&luid, device.luid()->data(), sizeof(luid));

    ComPtr<IDXGIFactory4> factory;
    check_hr(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)), "create a DXGI factory");
    ComPtr<IDXGIAdapter1> adapter;
    check_hr(factory->EnumAdapterByLuid(luid, IID_PPV_ARGS(&adapter)), "find the Direct3D adapter");
    check_hr(D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                               nullptr, 0, D3D11_SDK_VERSION, &impl_->d3d, nullptr, &impl_->context),
             "create a Direct3D 11 device");
    D3D11_QUERY_DESC query{D3D11_QUERY_EVENT, 0};
    check_hr(impl_->d3d->CreateQuery(&query, &impl_->done), "create a Direct3D query");

    ComPtr<IDXGIDevice> dxgi;
    check_hr(impl_->d3d.As(&dxgi), "get the DXGI device");
    check_hr(DCompositionCreateDevice(dxgi.Get(), IID_PPV_ARGS(&impl_->dcomp)), "create a DirectComposition device");
    check_hr(impl_->dcomp->CreateTargetForHwnd(static_cast<HWND>(hwnd), TRUE, &impl_->target),
             "attach DirectComposition to the window");
    check_hr(impl_->dcomp->CreateVisual(&impl_->root), "create a composition visual");
    check_hr(impl_->dcomp->CreateVisual(&impl_->visual), "create a composition visual");
    if (!transparent) {
        // Under the frames: the window's background, wherever a frame doesn't reach (yet).
        check_hr(impl_->dcomp->CreateVisual(&impl_->background), "create a composition visual");
        check_hr(impl_->dcomp->CreateSurface(1, 1, DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_ALPHA_MODE_IGNORE,
                                             &impl_->background_surface),
                 "create the background surface");
        check_hr(impl_->background->SetContent(impl_->background_surface.Get()), "show the background");
        // Bitmap interpolation off: the single pixel scales to a flat color.
        check_hr(impl_->background->SetBitmapInterpolationMode(DCOMPOSITION_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR),
                 "set the background's interpolation");
        const D2D_MATRIX_3X2_F scale{16384, 0, 0, 16384, 0, 0};
        check_hr(impl_->background->SetTransform(scale), "scale the background");
        check_hr(impl_->root->AddVisual(impl_->background.Get(), FALSE, nullptr), "add the background");
    }
    check_hr(impl_->root->AddVisual(impl_->visual.Get(), TRUE, impl_->background.Get()), "add the frames");
    check_hr(impl_->target->SetRoot(impl_->root.Get()), "set the composition root");
    check_hr(impl_->dcomp->Commit(), "commit the composition");
    log_debug("window presented through DirectComposition ({})", transparent ? "transparent" : "opaque");
}

CompositionTarget::~CompositionTarget() {
    destroy_image();
    if (impl_->ready) {
        CloseHandle(impl_->ready);
    }
}

void CompositionTarget::wait_until_ready(unsigned timeout_ms) {
    if (impl_->ready) {
        WaitForSingleObjectEx(impl_->ready, timeout_ms, TRUE);
    }
}

void CompositionTarget::set_background(const std::array<float, 4>& srgb) {
    if (!impl_->background_surface || background_ == srgb) {
        return;
    }
    background_ = srgb;
    ComPtr<ID3D11Texture2D> texture;
    POINT offset{};
    check_hr(impl_->background_surface->BeginDraw(nullptr, IID_PPV_ARGS(&texture), &offset),
             "draw the background");
    ComPtr<ID3D11RenderTargetView> target;
    D3D11_RENDER_TARGET_VIEW_DESC desc{};
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    const HRESULT created = impl_->d3d->CreateRenderTargetView(texture.Get(), &desc, &target);
    if (SUCCEEDED(created)) {
        // The surface may be a region of a bigger atlas: clear just our pixel.
        const D3D11_RECT pixel{offset.x, offset.y, offset.x + 1, offset.y + 1};
        ComPtr<ID3D11DeviceContext1> context1;
        if (SUCCEEDED(impl_->context.As(&context1))) {
            context1->ClearView(target.Get(), srgb.data(), &pixel, 1);
        } else {
            impl_->context->ClearRenderTargetView(target.Get(), srgb.data());
        }
    }
    check_hr(impl_->background_surface->EndDraw(), "finish the background");
    check_hr(created, "create the background's render target");
    check_hr(impl_->dcomp->Commit(), "commit the composition");
}

void CompositionTarget::destroy_image() noexcept {
    end_access();
    impl_->view = nullptr;
    impl_->texture = nullptr;
    impl_->memory = nullptr;
    impl_->shared.Reset();
}

wgpu::TextureView CompositionTarget::begin_access() {
    // Its previous contents don't matter: every frame clears it.
    wgpu::SharedTextureMemoryBeginAccessDescriptor access;
    access.initialized = false;
    if (impl_->memory.BeginAccess(impl_->texture, &access) != wgpu::Status::Success) {
        throw_gpu_error("access the shared texture");
    }
    impl_->accessing = true;
    return impl_->view;
}

void CompositionTarget::end_access() noexcept {
    if (impl_->accessing) {
        // Direct3D copies the texture only after the CPU waited for the rendering (present()),
        // so the end state's fences aren't needed.
        wgpu::SharedTextureMemoryEndAccessState state;
        impl_->memory.EndAccess(impl_->texture, &state);
        impl_->accessing = false;
    }
}

bool CompositionTarget::prepare(std::uint32_t width, std::uint32_t height) {
    if (width == 0 || height == 0) {
        return false;
    }
    if (impl_->texture && width == width_ && height == height_) {
        return true;
    }
    // Nothing uses the old image: every frame waits for its rendering before presenting.
    destroy_image();
    width_ = width;
    height_ = height;

    // The composition swapchain: premultiplied alpha, which DWM blends with what's behind.
    if (!impl_->swapchain) {
        ComPtr<IDXGIDevice> dxgi;
        check_hr(impl_->d3d.As(&dxgi), "get the DXGI device");
        ComPtr<IDXGIAdapter> adapter;
        check_hr(dxgi->GetAdapter(&adapter), "get the DXGI adapter");
        ComPtr<IDXGIFactory2> factory;
        check_hr(adapter->GetParent(IID_PPV_ARGS(&factory)), "get the DXGI factory");
        DXGI_SWAP_CHAIN_DESC1 desc{};
        desc.Width = width;
        desc.Height = height;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 2;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        desc.AlphaMode = transparent_ ? DXGI_ALPHA_MODE_PREMULTIPLIED : DXGI_ALPHA_MODE_IGNORE;
        desc.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
        check_hr(factory->CreateSwapChainForComposition(impl_->d3d.Get(), &desc, nullptr, &impl_->swapchain),
                 "create a composition swapchain");
        ComPtr<IDXGISwapChain2> swapchain2;
        if (SUCCEEDED(impl_->swapchain.As(&swapchain2))) {
            swapchain2->SetMaximumFrameLatency(1);
            impl_->ready = swapchain2->GetFrameLatencyWaitableObject();
        }
        check_hr(impl_->visual->SetContent(impl_->swapchain.Get()), "show the swapchain");
        check_hr(impl_->dcomp->Commit(), "commit the composition");
    } else {
        check_hr(impl_->swapchain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN,
                                                 DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT),
                 "resize the composition swapchain");
    }

    // The shared texture, created by D3D11 with an NT handle...
    D3D11_TEXTURE2D_DESC texture{};
    texture.Width = width;
    texture.Height = height;
    texture.MipLevels = 1;
    texture.ArraySize = 1;
    texture.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    texture.SampleDesc.Count = 1;
    texture.Usage = D3D11_USAGE_DEFAULT;
    texture.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    texture.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
    check_hr(impl_->d3d->CreateTexture2D(&texture, nullptr, &impl_->shared), "create the shared texture");
    ComPtr<IDXGIResource1> resource;
    check_hr(impl_->shared.As(&resource), "get the shared texture's DXGI resource");
    HANDLE handle = nullptr;
    check_hr(resource->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, nullptr,
                                          &handle),
             "share the texture");

    // ...and imported into WebGPU as a texture we render to.
    wgpu::SharedTextureMemoryDXGISharedHandleDescriptor shared;
    shared.handle = handle;
    wgpu::SharedTextureMemoryDescriptor memory;
    memory.nextInChain = &shared;
    memory.label = "composition target";
    impl_->memory = device_.handle().ImportSharedTextureMemory(&memory);
    CloseHandle(handle); // importing an NT handle doesn't take ownership of it
    if (!impl_->memory) {
        destroy_image();
        throw_gpu_error("import the shared texture");
    }
    const wgpu::TextureFormat sampled = view_format();
    wgpu::TextureDescriptor descriptor;
    descriptor.label = "composition target";
    descriptor.dimension = wgpu::TextureDimension::e2D;
    descriptor.size = {width, height, 1};
    descriptor.format = wgpu::TextureFormat::BGRA8Unorm; // UNORM storage, sRGB view
    descriptor.usage = wgpu::TextureUsage::RenderAttachment;
    descriptor.viewFormatCount = 1;
    descriptor.viewFormats = &sampled;
    impl_->texture = impl_->memory.CreateTexture(&descriptor);
    wgpu::TextureViewDescriptor view;
    view.format = sampled;
    impl_->view = impl_->texture.CreateView(&view);
    return true;
}

void CompositionTarget::present() {
    ComPtr<ID3D11Texture2D> back_buffer;
    check_hr(impl_->swapchain->GetBuffer(0, IID_PPV_ARGS(&back_buffer)), "get the composition back buffer");
    impl_->context->CopyResource(back_buffer.Get(), impl_->shared.Get());
    // Sync interval 1: shown at the next vblank, and the frame-latency object wait_until_ready()
    // waits on is signaled only once it's on screen, so animation runs at the display's rate. With
    // 0 that object is signaled as soon as the present is queued, and an animating window renders
    // as fast as the GPU allows (hundreds of frames a second). Latency stays at one frame: the wait
    // returns at a vblank, and the frame rendered right after it shows at the next.
    check_hr(impl_->swapchain->Present(1, 0), "present to the composition swapchain");
    // WebGPU renders into the shared texture again next frame: let the copy finish first.
    impl_->context->End(impl_->done.Get());
    while (impl_->context->GetData(impl_->done.Get(), nullptr, 0, 0) == S_FALSE) {
        SwitchToThread();
    }
}

} // namespace elg::detail::gpu

#endif // _WIN32
