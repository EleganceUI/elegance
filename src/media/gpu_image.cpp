// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// GPU images (elg/gpu_image.h). Windows: a ring of shared Direct3D 11 textures and one shared
// fence, made on the renderer's adapter. The app signals the fence when a frame is drawn; the
// renderer waits on it (on the GPU) before reading, and reports its own fence when done, which
// begin_frame() waits on before handing the buffer out again. When the renderer can't import
// shared textures (Dawn's Vulkan backend), end_frame() copies frames to memory instead.

#include <elg/gpu_image.h>

#include "core/log.h"
#include "media/video_source.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
#include <thread>
#include <unordered_map>

#if defined(_WIN32)
#include <windows.h>
#include <d3d11_4.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#endif

namespace elg {

namespace detail {

namespace {

std::atomic<std::uint64_t> next_texture_id{1ull << 62}; // apart from decoders' ids

} // namespace

class GpuImageSource final : public VideoSource, public SharedTextureOwner {
public:
    explicit GpuImageSource(GpuImage::Options options) : options_(options) {
        size = Size{static_cast<float>(options.width), static_cast<float>(options.height)};
        ready = true;
    }

    ~GpuImageSource() override {
#if defined(_WIN32)
        for (Buffer& buffer : buffers_) {
            if (buffer.handle) {
                CloseHandle(buffer.handle);
            }
        }
        if (fence_handle_) {
            CloseHandle(fence_handle_);
        }
#endif
    }

    const GpuImage::Options& options() const noexcept { return options_; }

    void play() override {}
    void pause() override {}
    void seek(double) override {}
    void set_volume(float) override {}
    void set_muted(bool) override {}
    void set_looping(bool) override {}

    // --- The app's drawing thread ---

    std::optional<GpuImage::Frame> begin_frame() {
#if defined(_WIN32)
        if (options_.width == 0 || options_.height == 0 || !create()) {
            return std::nullopt;
        }
        int chosen = -1;
        Microsoft::WRL::ComPtr<ID3D11Fence> read_fence;
        std::uint64_t read_value = 0;
        std::uint64_t value = 0;
        for (;;) {
            {
                std::lock_guard lock(mutex_);
                // Not shown, not about to be, not being read: the one used longest ago.
                for (int i = 0; i < static_cast<int>(GpuImage::kBuffers); ++i) {
                    const Buffer& buffer = buffers_[i];
                    if (i == current_ || i == latest_ || buffer.reading > 0) {
                        continue;
                    }
                    if (chosen < 0 || buffer.written < buffers_[chosen].written) {
                        chosen = i;
                    }
                }
                if (chosen >= 0) {
                    writing_ = chosen;
                    value = ++fence_value_;
                    read_fence = buffers_[chosen].read_fence;
                    read_value = buffers_[chosen].read_value;
                    break;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1)); // all busy: rare, and brief
        }
        // The renderer's last read of it.
        if (read_fence && read_fence->GetCompletedValue() < read_value) {
            if (SUCCEEDED(read_fence->SetEventOnCompletion(read_value, event_))) {
                WaitForSingleObject(event_, INFINITE);
            }
        }
        GpuImage::Frame frame;
        frame.buffer = static_cast<std::uint32_t>(chosen);
        frame.width = options_.width;
        frame.height = options_.height;
        frame.texture = buffers_[chosen].handle;
        frame.fence = fence_handle_;
        frame.fence_value = value;
        frame.adapter_luid = luid_;
        return frame;
#else
        return std::nullopt;
#endif
    }

    void end_frame(const GpuImage::Frame& frame, GpuImage::Sync sync) {
#if defined(_WIN32)
        if (!device_ || frame.buffer >= GpuImage::kBuffers) {
            return;
        }
        {
            std::lock_guard lock(mutex_);
            if (writing_ != static_cast<int>(frame.buffer)) {
                return; // not the frame begun last
            }
            writing_ = -1;
        }
        Buffer& buffer = buffers_[frame.buffer];
        if (sync == GpuImage::Sync::finished) {
            context_->Signal(fence_.Get(), frame.fence_value);
            context_->Flush();
        }
        if (!shares_ && !read_back(buffer, frame.fence_value)) {
            return;
        }
        {
            std::lock_guard lock(mutex_);
            buffer.ready_value = frame.fence_value;
            buffer.written = ++number_;
            latest_ = static_cast<int>(frame.buffer);
        }
        // The render thread checks for frames while it's set.
        playing_now_ = true;
#else
        (void)frame;
        (void)sync;
#endif
    }

    // --- Render thread ---

    bool update() override {
        std::lock_guard lock(mutex_);
        if (latest_ >= 0) {
            current_ = latest_;
            latest_ = -1;
        }
        if (number_ == reported_) {
            return false;
        }
        reported_ = number_;
        return true;
    }

    std::optional<VideoFrame> current() const override {
        std::lock_guard lock(mutex_);
        if (current_ < 0) {
            return std::nullopt;
        }
        VideoFrame frame;
        frame.width = options_.width;
        frame.height = options_.height;
        frame.source_id = id;
#if defined(_WIN32)
        const Buffer& buffer = buffers_[current_];
        if (!shares_) {
            frame.kind = VideoFrame::Kind::cpu;
            frame.format = VideoFrame::Format::bgra;
            frame.pixels = pixels_;
            frame.frame_id = buffer.written;
            return frame;
        }
        frame.kind = VideoFrame::Kind::d3d11_shared;
        frame.format = VideoFrame::Format::rgba;
        frame.shared_handle = buffer.handle;
        frame.texture_id = buffer.texture_id;
        frame.wait_fence = fence_handle_;
        frame.wait_fence_id = fence_id_;
        frame.wait_value = buffer.ready_value;
        frame.frame_id = buffer.written;
        frame.owner = std::static_pointer_cast<GpuImageSource>(std::const_pointer_cast<VideoSource>(shared_from_this()));
#endif
        return frame;
    }

    void began_reading(std::uint64_t texture_id) override {
        std::lock_guard lock(mutex_);
        if (Buffer* buffer = find(texture_id)) {
            ++buffer->reading;
        }
    }

    void finished_reading(std::uint64_t texture_id, [[maybe_unused]] void* fence, std::uint64_t value) override {
#if defined(_WIN32)
        // Opened here, while the handle is valid (the device is free-threaded).
        Microsoft::WRL::ComPtr<ID3D11Fence> opened;
        if (fence) {
            std::lock_guard lock(mutex_);
            if (const auto it = read_fences_.find(fence); it != read_fences_.end()) {
                opened = it->second;
            }
        }
        if (fence && !opened && device_ && SUCCEEDED(device_->OpenSharedFence(fence, IID_PPV_ARGS(&opened)))) {
            std::lock_guard lock(mutex_);
            read_fences_.emplace(fence, opened);
        }
        std::lock_guard lock(mutex_);
        if (Buffer* buffer = find(texture_id)) {
            buffer->reading = std::max(buffer->reading - 1, 0);
            buffer->read_fence = opened;
            buffer->read_value = value;
        }
#else
        (void)texture_id;
        (void)value;
#endif
    }

private:
#if defined(_WIN32)
    struct Buffer {
        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
        HANDLE handle = nullptr;
        std::uint64_t texture_id = 0;
        std::uint64_t ready_value = 0; // the fence value its contents are drawn at
        std::uint64_t written = 0;     // frame number
        int reading = 0;
        Microsoft::WRL::ComPtr<ID3D11Fence> read_fence; // the renderer's, done reading at read_value
        std::uint64_t read_value = 0;
    };

    Buffer* find(std::uint64_t texture_id) {
        for (Buffer& buffer : buffers_) {
            if (buffer.texture_id == texture_id) {
                return &buffer;
            }
        }
        return nullptr;
    }

    // The textures and fence, on the renderer's adapter, once it's known. Drawing thread.
    bool create() {
        if (device_) {
            return true;
        }
        if (failed_) {
            return false;
        }
        const std::optional<RenderAdapter> adapter = render_adapter();
        if (!adapter) {
            return false; // no window has drawn yet
        }
        const auto fail = [&](const char* what, HRESULT result) {
            log_error("GpuImage: can't {} (0x{:08x})", what, static_cast<std::uint32_t>(result));
            failed_ = true;
            return false;
        };
        LUID luid;
        std::memcpy(&luid, adapter->luid.data(), sizeof(luid));
        Microsoft::WRL::ComPtr<IDXGIFactory4> factory;
        HRESULT result = CreateDXGIFactory2(0, IID_PPV_ARGS(&factory));
        Microsoft::WRL::ComPtr<IDXGIAdapter1> dxgi_adapter;
        if (SUCCEEDED(result)) {
            result = factory->EnumAdapterByLuid(luid, IID_PPV_ARGS(&dxgi_adapter));
        }
        if (FAILED(result)) {
            return fail("find the renderer's GPU", result);
        }
        Microsoft::WRL::ComPtr<ID3D11Device> device;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
        result = D3D11CreateDevice(dxgi_adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, nullptr, 0,
                                   D3D11_SDK_VERSION, &device, nullptr, &context);
        Microsoft::WRL::ComPtr<ID3D11Device5> device5;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext4> context4;
        if (SUCCEEDED(result)) {
            result = device.As(&device5);
        }
        if (SUCCEEDED(result)) {
            result = context.As(&context4);
        }
        if (FAILED(result)) {
            return fail("create a Direct3D 11.4 device", result);
        }
        Microsoft::WRL::ComPtr<ID3D10Multithread> multithread;
        if (SUCCEEDED(device.As(&multithread))) {
            multithread->SetMultithreadProtected(TRUE); // the render thread opens fences on it
        }

        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = options_.width;
        desc.Height = options_.height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
        for (Buffer& buffer : buffers_) {
            result = device5->CreateTexture2D(&desc, nullptr, &buffer.texture);
            Microsoft::WRL::ComPtr<IDXGIResource1> resource;
            if (SUCCEEDED(result)) {
                result = buffer.texture.As(&resource);
            }
            if (SUCCEEDED(result)) {
                result = resource->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
                                                      nullptr, &buffer.handle);
            }
            if (FAILED(result)) {
                return fail("create a shared texture", result);
            }
            buffer.texture_id = next_texture_id++;
        }
        result = device5->CreateFence(0, D3D11_FENCE_FLAG_SHARED, IID_PPV_ARGS(&fence_));
        if (SUCCEEDED(result)) {
            result = fence_->CreateSharedHandle(nullptr, GENERIC_ALL, nullptr, &fence_handle_);
        }
        if (FAILED(result)) {
            return fail("create a shared fence", result);
        }
        fence_id_ = next_texture_id++;
        event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);

        shares_ = adapter->shares_textures;
        if (!shares_) {
            desc.Usage = D3D11_USAGE_STAGING;
            desc.BindFlags = 0;
            desc.MiscFlags = 0;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            result = device5->CreateTexture2D(&desc, nullptr, &staging_);
            if (FAILED(result)) {
                return fail("create a staging texture", result);
            }
            log_info("GpuImage: the GPU backend can't share textures; frames are copied through memory");
        }
        luid_ = adapter->luid;
        context_ = context4;
        device_ = device5; // last: create() is done
        return true;
    }

    // The renderer can't import the texture: copy it to memory, as BGRA. Drawing thread.
    bool read_back(const Buffer& buffer, std::uint64_t value) {
        context_->Wait(fence_.Get(), value);
        context_->CopyResource(staging_.Get(), buffer.texture.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(context_->Map(staging_.Get(), 0, D3D11_MAP_READ, 0, &mapped))) { // waits for the copy
            return false;
        }
        auto pixels = std::make_shared<std::vector<std::uint8_t>>(std::size_t{options_.width} * options_.height * 4);
        for (std::uint32_t y = 0; y < options_.height; ++y) {
            const auto* from = static_cast<const std::uint8_t*>(mapped.pData) + std::size_t{y} * mapped.RowPitch;
            std::uint8_t* to = pixels->data() + std::size_t{y} * options_.width * 4;
            for (std::uint32_t x = 0; x < options_.width; ++x, from += 4, to += 4) {
                to[0] = from[2];
                to[1] = from[1];
                to[2] = from[0];
                to[3] = from[3];
            }
        }
        context_->Unmap(staging_.Get(), 0);
        std::lock_guard lock(mutex_);
        pixels_ = std::move(pixels);
        return true;
    }

    Microsoft::WRL::ComPtr<ID3D11Device5> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext4> context_; // drawing thread only
    Microsoft::WRL::ComPtr<ID3D11Fence> fence_;
    HANDLE fence_handle_ = nullptr;
    std::uint64_t fence_id_ = 0;
    HANDLE event_ = nullptr;
    std::array<std::uint8_t, 8> luid_{};
    bool shares_ = true;
    bool failed_ = false;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> staging_;
    std::shared_ptr<const std::vector<std::uint8_t>> pixels_;
    std::unordered_map<void*, Microsoft::WRL::ComPtr<ID3D11Fence>> read_fences_; // by the renderer's handle
    std::array<Buffer, GpuImage::kBuffers> buffers_;
#endif

    const GpuImage::Options options_;
    mutable std::mutex mutex_;
    std::uint64_t fence_value_ = 0;
    int writing_ = -1; // the app is drawing it
    int latest_ = -1;  // drawn, not shown yet
    int current_ = -1; // shown
    std::uint64_t number_ = 0;
    std::uint64_t reported_ = 0;
};

} // namespace detail

GpuImage::GpuImage(Options options) : source_(std::make_shared<detail::GpuImageSource>(options)) {}

std::optional<GpuImage::Frame> GpuImage::begin_frame() const {
    return source_->begin_frame();
}

void GpuImage::end_frame(const Frame& frame, Sync sync) const {
    source_->end_frame(frame, sync);
}

Size GpuImage::size() const noexcept {
    return {static_cast<float>(source_->options().width), static_cast<float>(source_->options().height)};
}

GpuImage::Origin GpuImage::origin() const noexcept {
    return source_->options().origin;
}

std::shared_ptr<detail::VideoSource> GpuImage::source() const noexcept {
    return source_;
}

} // namespace elg
