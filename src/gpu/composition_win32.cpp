// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#if defined(_WIN32)

#include "gpu/composition_win32.h"

#include "core/log.h"

#include <elg/error.h>

#include <windows.h> // the build defines WIN32_LEAN_AND_MEAN and NOMINMAX
#include <d3d11_1.h>
#include <dcomp.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#include <vulkan/vulkan_win32.h>

#include <cstring>
#include <format>

namespace elg::detail::gpu {

using Microsoft::WRL::ComPtr;

namespace {

void check_hr(HRESULT result, std::string_view what) {
    if (FAILED(result)) {
        throw GpuError(std::format("failed to {} (HRESULT 0x{:08x})", what, static_cast<unsigned long>(result)));
    }
}

constexpr const char* kExternalMemoryWin32 = "VK_KHR_external_memory_win32";

} // namespace

struct CompositionTarget::Impl {
    // D3D11 on the same adapter as the Vulkan device.
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

    // The shared texture: created by D3D11, imported into Vulkan.
    ComPtr<ID3D11Texture2D> shared;
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;

    PFN_vkGetMemoryWin32HandlePropertiesKHR get_handle_properties = nullptr;
};

bool CompositionTarget::supported(const Device& device) {
    return device.has_extension(kExternalMemoryWin32);
}

CompositionTarget::CompositionTarget(const Device& device, void* hwnd, bool transparent)
    : device_(device), impl_(std::make_unique<Impl>()), transparent_(transparent) {
    if (!supported(device)) {
        throw GpuError("the GPU can't share images with Direct3D (no VK_KHR_external_memory_win32)");
    }
    // Loaded by hand: the Vulkan loader library may be built without the Win32 platform.
    impl_->get_handle_properties = reinterpret_cast<PFN_vkGetMemoryWin32HandlePropertiesKHR>(
        vkGetDeviceProcAddr(device.handle(), "vkGetMemoryWin32HandlePropertiesKHR"));
    if (!impl_->get_handle_properties) {
        throw GpuError("vkGetMemoryWin32HandlePropertiesKHR is missing");
    }

    // The Vulkan device's adapter, by LUID.
    VkPhysicalDeviceIDProperties ids{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
    VkPhysicalDeviceProperties2 properties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    properties.pNext = &ids;
    vkGetPhysicalDeviceProperties2(device.physical(), &properties);
    if (!ids.deviceLUIDValid) {
        throw GpuError("the GPU has no adapter LUID to match with Direct3D");
    }
    LUID luid;
    static_assert(sizeof(luid) == VK_LUID_SIZE);
    std::memcpy(&luid, ids.deviceLUID, sizeof(luid));

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
    const VkDevice dev = device_.handle();
    if (impl_->view) {
        vkDestroyImageView(dev, impl_->view, nullptr);
        impl_->view = VK_NULL_HANDLE;
    }
    if (impl_->image) {
        vkDestroyImage(dev, impl_->image, nullptr);
        impl_->image = VK_NULL_HANDLE;
    }
    if (impl_->memory) {
        vkFreeMemory(dev, impl_->memory, nullptr);
        impl_->memory = VK_NULL_HANDLE;
    }
    impl_->shared.Reset();
}

VkImage CompositionTarget::image() const noexcept {
    return impl_->image;
}

VkImageView CompositionTarget::view() const noexcept {
    return impl_->view;
}

bool CompositionTarget::prepare(VkExtent2D extent) {
    if (extent.width == 0 || extent.height == 0) {
        return false;
    }
    if (impl_->image && extent.width == extent_.width && extent.height == extent_.height) {
        return true;
    }
    // Nothing uses the old image: every frame waits for its rendering before presenting.
    destroy_image();
    extent_ = extent;

    // The composition swapchain: premultiplied alpha, which DWM blends with what's behind.
    if (!impl_->swapchain) {
        ComPtr<IDXGIDevice> dxgi;
        check_hr(impl_->d3d.As(&dxgi), "get the DXGI device");
        ComPtr<IDXGIAdapter> adapter;
        check_hr(dxgi->GetAdapter(&adapter), "get the DXGI adapter");
        ComPtr<IDXGIFactory2> factory;
        check_hr(adapter->GetParent(IID_PPV_ARGS(&factory)), "get the DXGI factory");
        DXGI_SWAP_CHAIN_DESC1 desc{};
        desc.Width = extent.width;
        desc.Height = extent.height;
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
        check_hr(impl_->swapchain->ResizeBuffers(0, extent.width, extent.height, DXGI_FORMAT_UNKNOWN,
                                                 DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT),
                 "resize the composition swapchain");
    }

    // The shared texture, created by D3D11 with an NT handle...
    D3D11_TEXTURE2D_DESC texture{};
    texture.Width = extent.width;
    texture.Height = extent.height;
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

    // ...and imported into Vulkan as an image we render to.
    const VkDevice dev = device_.handle();
    try {
        VkExternalMemoryImageCreateInfo external{VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO};
        external.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT;
        VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        info.pNext = &external;
        info.flags = VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT; // UNORM storage, sRGB view
        info.imageType = VK_IMAGE_TYPE_2D;
        info.format = VK_FORMAT_B8G8R8A8_UNORM;
        info.extent = {extent.width, extent.height, 1};
        info.mipLevels = 1;
        info.arrayLayers = 1;
        info.samples = VK_SAMPLE_COUNT_1_BIT;
        info.tiling = VK_IMAGE_TILING_OPTIMAL;
        info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        check(vkCreateImage(dev, &info, nullptr, &impl_->image), "create the shared image");

        VkMemoryRequirements requirements;
        vkGetImageMemoryRequirements(dev, impl_->image, &requirements);
        VkMemoryWin32HandlePropertiesKHR handle_properties{VK_STRUCTURE_TYPE_MEMORY_WIN32_HANDLE_PROPERTIES_KHR};
        check(impl_->get_handle_properties(dev, VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT, handle,
                                           &handle_properties),
              "query the shared texture's memory");
        const std::uint32_t allowed = requirements.memoryTypeBits & handle_properties.memoryTypeBits;
        VkPhysicalDeviceMemoryProperties memory_properties;
        vkGetPhysicalDeviceMemoryProperties(device_.physical(), &memory_properties);
        std::uint32_t type = UINT32_MAX;
        for (std::uint32_t i = 0; i < memory_properties.memoryTypeCount; ++i) {
            if ((allowed & (1u << i)) == 0) {
                continue;
            }
            if (type == UINT32_MAX ||
                (memory_properties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
                type = i;
            }
        }
        if (type == UINT32_MAX) {
            throw GpuError("no memory type can hold the shared texture");
        }

        VkImportMemoryWin32HandleInfoKHR import{VK_STRUCTURE_TYPE_IMPORT_MEMORY_WIN32_HANDLE_INFO_KHR};
        import.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT;
        import.handle = handle;
        VkMemoryDedicatedAllocateInfo dedicated{VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO};
        dedicated.pNext = &import;
        dedicated.image = impl_->image;
        VkMemoryAllocateInfo allocate{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocate.pNext = &dedicated;
        allocate.allocationSize = requirements.size;
        allocate.memoryTypeIndex = type;
        check(vkAllocateMemory(dev, &allocate, nullptr, &impl_->memory), "import the shared texture");
        check(vkBindImageMemory(dev, impl_->image, impl_->memory, 0), "bind the shared texture");

        VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view.image = impl_->image;
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = format();
        view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        check(vkCreateImageView(dev, &view, nullptr, &impl_->view), "create the shared image view");
    } catch (...) {
        CloseHandle(handle);
        destroy_image();
        throw;
    }
    CloseHandle(handle); // importing an NT handle doesn't take ownership of it
    return true;
}

void CompositionTarget::present() {
    ComPtr<ID3D11Texture2D> back_buffer;
    check_hr(impl_->swapchain->GetBuffer(0, IID_PPV_ARGS(&back_buffer)), "get the composition back buffer");
    impl_->context->CopyResource(back_buffer.Get(), impl_->shared.Get());
    // No vsync wait here: DirectComposition shows it at the next composition anyway, and waiting
    // would add a frame of latency (hover feedback in menus felt it). Frames are paced by
    // wait_until_ready() before rendering instead.
    check_hr(impl_->swapchain->Present(0, 0), "present to the composition swapchain");
    // Vulkan renders into the shared texture again next frame: let the copy finish first.
    impl_->context->End(impl_->done.Get());
    while (impl_->context->GetData(impl_->done.Get(), nullptr, 0, 0) == S_FALSE) {
        SwitchToThread();
    }
}

} // namespace elg::detail::gpu

#endif // _WIN32
