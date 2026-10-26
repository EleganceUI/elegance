// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Video sources (design.md §13.3). Windows: Media Foundation's media engine in frame-server
// mode, rendering each new frame into a ring of D3D11 textures shared with WebGPU. macOS:
// AVFoundation, in video_source_apple.mm.

#include "media/video_source.h"

#include <atomic>
#include <mutex>

#if defined(_WIN32)
#include "core/log.h"
#include "core/ui_tasks.h"

#include <cmath>
#include <format>
#include <windows.h>
#include <d3d11_4.h>
#include <dxgi1_4.h>
#include <mfapi.h>
#include <mfmediaengine.h>
#include <wrl/client.h>
#endif

namespace elg::detail {

namespace {

std::atomic<std::uint64_t> g_next_id{1};
std::mutex g_adapter_mutex;
std::optional<RenderAdapter> g_adapter;

} // namespace

void set_render_adapter(RenderAdapter adapter) {
    std::lock_guard lock(g_adapter_mutex);
    g_adapter = adapter;
}

std::optional<RenderAdapter> render_adapter() {
    std::lock_guard lock(g_adapter_mutex);
    return g_adapter;
}

VideoSource::VideoSource() : id(g_next_id.fetch_add(1)) {}

std::size_t packed_size(VideoFrame::Format format, std::uint32_t width, std::uint32_t height) {
    const std::size_t pixels = static_cast<std::size_t>(width) * height;
    switch (format) {
    case VideoFrame::Format::rgba:
    case VideoFrame::Format::bgra: return pixels * 4;
    case VideoFrame::Format::yuy2: return pixels * 2;
    case VideoFrame::Format::nv12:
    case VideoFrame::Format::i420: return pixels + 2 * (static_cast<std::size_t>((width + 1) / 2) * ((height + 1) / 2));
    }
    return 0;
}

namespace {

// Platforms without a video backend yet: an error, shown by the view.
class UnsupportedVideoSource final : public VideoSource {
public:
    explicit UnsupportedVideoSource(std::string message) { error = std::move(message); }
    void play() override {}
    void pause() override {}
    void seek(double) override {}
    void set_volume(float v) override { volume = v; }
    void set_muted(bool m) override { muted = m; }
    void set_looping(bool l) override { looping = l; }
    bool update() override { return false; }
    std::optional<VideoFrame> current() const override { return std::nullopt; }
};

#if defined(_WIN32)

using Microsoft::WRL::ComPtr;

// Media Foundation, started once per process.
bool start_media_foundation() {
    static const bool started = [] {
        const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        (void)com;
        return SUCCEEDED(MFStartup(MF_VERSION, MFSTARTUP_FULL));
    }();
    return started;
}

std::string hr_text(HRESULT result) {
    return std::format("HRESULT 0x{:08x}", static_cast<unsigned long>(result));
}

class MediaEngineSource;

// The engine's event callback (on Media Foundation's threads). Holds the source weakly.
class EngineNotify final : public IMFMediaEngineNotify {
public:
    explicit EngineNotify(std::weak_ptr<MediaEngineSource> source) : source_(std::move(source)) {}

    STDMETHODIMP QueryInterface(REFIID riid, void** object) override {
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IMFMediaEngineNotify)) {
            *object = static_cast<IMFMediaEngineNotify*>(this);
            AddRef();
            return S_OK;
        }
        *object = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++references_; }
    STDMETHODIMP_(ULONG) Release() override {
        const ULONG left = --references_;
        if (left == 0) {
            delete this;
        }
        return left;
    }
    STDMETHODIMP EventNotify(DWORD event, DWORD_PTR param1, DWORD param2) override;

private:
    std::atomic<ULONG> references_{1};
    std::weak_ptr<MediaEngineSource> source_;
};

class MediaEngineSource final : public VideoSource {
public:
    explicit MediaEngineSource(std::string url) : url_(std::move(url)) {}

    ~MediaEngineSource() override {
        std::lock_guard lock(mutex_);
        if (engine_) {
            engine_->Shutdown();
            engine_raw_ = nullptr;
        }
        for (Slot& slot : ring_) {
            if (slot.handle) {
                CloseHandle(slot.handle);
            }
        }
    }

    // --- UI thread ---

    void play() override {
        std::lock_guard lock(mutex_);
        want_playing_ = true;
        if (ensure_engine()) {
            if (engine_->IsEnded()) {
                engine_->SetCurrentTime(0);
            }
            engine_->Play();
        }
        playing = true;
        playing_now_ = true;
    }

    void pause() override {
        std::lock_guard lock(mutex_);
        want_playing_ = false;
        if (ensure_engine()) {
            engine_->Pause();
        }
        playing = false;
        playing_now_ = false;
    }

    void seek(double seconds) override {
        std::lock_guard lock(mutex_);
        seconds = std::max(0.0, seconds);
        pending_seek_ = seconds;
        if (ensure_engine()) {
            engine_->SetCurrentTime(seconds);
            pending_seek_.reset();
        }
        time = seconds;
    }

    void set_volume(float value) override {
        std::lock_guard lock(mutex_);
        value = std::clamp(value, 0.f, 1.f);
        if (ensure_engine()) {
            engine_->SetVolume(value);
        }
        volume = value;
    }

    void set_muted(bool value) override {
        std::lock_guard lock(mutex_);
        if (ensure_engine()) {
            engine_->SetMuted(value);
        }
        muted = value;
    }

    void set_looping(bool value) override {
        std::lock_guard lock(mutex_);
        if (ensure_engine()) {
            engine_->SetLoop(value);
        }
        looping = value;
    }

    // --- Render thread ---

    bool update() override {
        std::lock_guard lock(mutex_);
        if (!ensure_engine()) {
            return false;
        }
        LONGLONG pts = 0;
        if (engine_->OnVideoStreamTick(&pts) != S_OK) {
            return false; // no new frame
        }
        DWORD width = 0;
        DWORD height = 0;
        if (FAILED(engine_->GetNativeVideoSize(&width, &height)) || width == 0 || height == 0) {
            return false;
        }
        if (!ensure_ring(width, height)) {
            return false;
        }
        const int slot = (current_ + 1) % kRing;
        const RECT target{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
        const MFARGB border{0, 0, 0, 255};
        if (FAILED(engine_->TransferVideoFrame(ring_[slot].texture.Get(), nullptr, &target, &border))) {
            return false;
        }
        if (!shares_textures_) {
            return read_back(ring_[slot].texture.Get(), slot);
        }
        // WebGPU samples it next: the copy must be done.
        context_->End(done_.Get());
        context_->Flush();
        while (context_->GetData(done_.Get(), nullptr, 0, 0) == S_FALSE) {
            SwitchToThread();
        }
        current_ = slot;
        return true;
    }

    std::optional<VideoFrame> current() const override {
        std::lock_guard lock(mutex_);
        if (current_ < 0) {
            return std::nullopt;
        }
        const Slot& slot = ring_[current_];
        VideoFrame frame;
        frame.width = ring_width_;
        frame.height = ring_height_;
        if (!shares_textures_) {
            frame.kind = VideoFrame::Kind::cpu;
            frame.format = VideoFrame::Format::bgra;
            frame.pixels = pixels_;
            frame.source_id = id;
            frame.frame_id = frame_id_;
            return frame;
        }
        frame.kind = VideoFrame::Kind::d3d11_shared;
        frame.shared_handle = slot.handle;
        frame.texture_id = slot.id;
        return frame;
    }

    // --- Media Foundation's threads ---

    void on_event(DWORD event, DWORD_PTR param1) {
        // Not under the mutex: the engine may call back while a UI-thread call into it holds it.
        IMFMediaEngine* const engine = engine_raw_.load();
        if (!engine) {
            return;
        }
        const std::weak_ptr<MediaEngineSource> weak =
            std::static_pointer_cast<MediaEngineSource>(shared_from_this());
        const auto post = [weak](auto action) {
            post_ui_task([weak, action = std::move(action)] {
                if (const auto source = weak.lock()) {
                    action(*source);
                }
            });
        };
        switch (event) {
        case MF_MEDIA_ENGINE_EVENT_LOADEDMETADATA:
        case MF_MEDIA_ENGINE_EVENT_DURATIONCHANGE: {
            const double length = engine->GetDuration();
            DWORD w = 0;
            DWORD h = 0;
            engine->GetNativeVideoSize(&w, &h);
            post([length, w, h](MediaEngineSource& source) {
                source.duration = std::isfinite(length) ? length : 0.0;
                source.size = Size{static_cast<float>(w), static_cast<float>(h)};
                source.ready = true;
            });
            break;
        }
        case MF_MEDIA_ENGINE_EVENT_TIMEUPDATE: {
            const double now = engine->GetCurrentTime();
            post([now](MediaEngineSource& source) { source.time = now; });
            break;
        }
        case MF_MEDIA_ENGINE_EVENT_PLAYING:
            playing_now_ = true;
            post([](MediaEngineSource& source) { source.playing = true; });
            break;
        case MF_MEDIA_ENGINE_EVENT_PAUSE:
        case MF_MEDIA_ENGINE_EVENT_ENDED: {
            playing_now_ = false;
            const double now = engine->GetCurrentTime();
            post([now](MediaEngineSource& source) {
                source.playing = false;
                source.time = now;
            });
            break;
        }
        case MF_MEDIA_ENGINE_EVENT_ERROR: {
            playing_now_ = false;
            const auto code = static_cast<MF_MEDIA_ENGINE_ERR>(param1);
            std::string message = std::format("can't play {} (media error {})", url_, static_cast<int>(code));
            if (code == MF_MEDIA_ENGINE_ERR_SRC_NOT_SUPPORTED) {
                message = std::format("can't play {}: the format isn't supported, or the file is missing", url_);
            }
            log_warning("video: {}", message);
            post([message](MediaEngineSource& source) {
                source.error = message;
                source.playing = false;
            });
            break;
        }
        default: break;
        }
    }

private:
    static constexpr int kRing = 3; // frames in flight on the GPU may still read the previous ones

    struct Slot {
        ComPtr<ID3D11Texture2D> texture;
        HANDLE handle = nullptr;
        std::uint64_t id = 0;
    };

    // With the mutex held. Creates the engine once the renderer's adapter is known.
    bool ensure_engine() {
        if (engine_) {
            return true;
        }
        if (failed_) {
            return false;
        }
        const std::optional<RenderAdapter> adapter = render_adapter();
        if (!adapter) {
            return false; // the renderer isn't up yet: what was asked for applies once it is
        }
        shares_textures_ = adapter->shares_textures;
        try {
            create_engine(adapter->luid);
        } catch (const std::exception& failure) {
            failed_ = true;
            const std::string message = failure.what();
            log_warning("video: {}", message);
            post_ui_task([weak = std::weak_ptr<VideoSource>(shared_from_this()), message] {
                if (const auto source = weak.lock()) {
                    source->error = message;
                }
            });
            return false;
        }
        // Apply what was asked for before the engine existed.
        engine_->SetVolume(volume.peek());
        engine_->SetMuted(muted.peek());
        engine_->SetLoop(looping.peek());
        if (pending_seek_) {
            engine_->SetCurrentTime(*pending_seek_);
            pending_seek_.reset();
        }
        if (want_playing_) {
            engine_->Play();
        }
        return true;
    }

    void create_engine(const std::array<std::uint8_t, 8>& adapter_luid) {
        const auto check = [](HRESULT result, std::string_view what) {
            if (FAILED(result)) {
                throw std::runtime_error(std::format("can't {} ({})", what, hr_text(result)));
            }
        };
        if (!start_media_foundation()) {
            throw std::runtime_error("Media Foundation isn't available (Windows N editions need the Media Feature Pack)");
        }
        LUID luid;
        std::memcpy(&luid, adapter_luid.data(), sizeof(luid));
        ComPtr<IDXGIFactory4> factory;
        check(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)), "create a DXGI factory");
        ComPtr<IDXGIAdapter1> adapter;
        check(factory->EnumAdapterByLuid(luid, IID_PPV_ARGS(&adapter)), "find the GPU for video");
        check(D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
                                D3D11_CREATE_DEVICE_VIDEO_SUPPORT | D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                                D3D11_SDK_VERSION, &device_, nullptr, &context_),
              "create a video device");
        ComPtr<ID3D10Multithread> multithread;
        if (SUCCEEDED(device_.As(&multithread))) {
            multithread->SetMultithreadProtected(TRUE); // the engine's threads share it with ours
        }
        D3D11_QUERY_DESC query{D3D11_QUERY_EVENT, 0};
        check(device_->CreateQuery(&query, &done_), "create a video query");

        UINT token = 0;
        check(MFCreateDXGIDeviceManager(&token, &manager_), "create a DXGI device manager");
        check(manager_->ResetDevice(device_.Get(), token), "share the video device");

        ComPtr<IMFMediaEngineClassFactory> engine_factory;
        check(CoCreateInstance(CLSID_MFMediaEngineClassFactory, nullptr, CLSCTX_INPROC_SERVER,
                               IID_PPV_ARGS(&engine_factory)),
              "create the media engine factory");
        ComPtr<IMFAttributes> attributes;
        check(MFCreateAttributes(&attributes, 3), "create media engine attributes");
        auto* notify = new EngineNotify(std::static_pointer_cast<MediaEngineSource>(shared_from_this()));
        attributes->SetUnknown(MF_MEDIA_ENGINE_CALLBACK, notify);
        notify->Release(); // the attributes and the engine hold it
        attributes->SetUnknown(MF_MEDIA_ENGINE_DXGI_MANAGER, manager_.Get());
        attributes->SetUINT32(MF_MEDIA_ENGINE_VIDEO_OUTPUT_FORMAT, DXGI_FORMAT_B8G8R8A8_UNORM);
        check(engine_factory->CreateInstance(0, attributes.Get(), &engine_), "create the media engine");
        engine_raw_ = engine_.Get();

        std::wstring url(url_.size(), L'\0');
        const int length = MultiByteToWideChar(CP_UTF8, 0, url_.data(), static_cast<int>(url_.size()), url.data(),
                                               static_cast<int>(url.size()));
        url.resize(static_cast<std::size_t>(std::max(length, 0)));
        BSTR source = SysAllocStringLen(url.data(), static_cast<UINT>(url.size()));
        const HRESULT set = engine_->SetSource(source);
        SysFreeString(source);
        check(set, "open the video");
        engine_->SetPreload(MF_MEDIA_ENGINE_PRELOAD_AUTOMATIC);
        engine_->Load();
    }

    // With the mutex held: the shared textures, at the video's size.
    bool ensure_ring(DWORD width, DWORD height) {
        if (ring_width_ == width && ring_height_ == height && ring_[0].texture) {
            return true;
        }
        for (Slot& slot : ring_) {
            if (slot.handle) {
                CloseHandle(slot.handle);
            }
            slot = {};
        }
        current_ = -1;
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
        for (Slot& slot : ring_) {
            if (FAILED(device_->CreateTexture2D(&desc, nullptr, &slot.texture))) {
                return false;
            }
            ComPtr<IDXGIResource1> resource;
            if (FAILED(slot.texture.As(&resource)) ||
                FAILED(resource->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
                                                    nullptr, &slot.handle))) {
                return false;
            }
            slot.id = g_next_id.fetch_add(1);
        }
        if (!shares_textures_) {
            desc.Usage = D3D11_USAGE_STAGING;
            desc.BindFlags = 0;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            desc.MiscFlags = 0;
            if (FAILED(device_->CreateTexture2D(&desc, nullptr, &staging_))) {
                return false;
            }
        }
        ring_width_ = width;
        ring_height_ = height;
        return true;
    }

    // With the mutex held. The renderer can't import the texture: copies the frame into memory,
    // packed, for the renderer to upload.
    bool read_back(ID3D11Texture2D* texture, int slot) {
        context_->CopyResource(staging_.Get(), texture);
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(context_->Map(staging_.Get(), 0, D3D11_MAP_READ, 0, &mapped))) { // waits for the copy
            return false;
        }
        const std::size_t row = std::size_t{ring_width_} * 4;
        auto pixels = std::make_shared<std::vector<std::uint8_t>>(row * ring_height_);
        for (std::uint32_t y = 0; y < ring_height_; ++y) {
            std::memcpy(pixels->data() + row * y, static_cast<const std::uint8_t*>(mapped.pData) + std::size_t{mapped.RowPitch} * y, row);
        }
        context_->Unmap(staging_.Get(), 0);
        pixels_ = std::move(pixels);
        ++frame_id_;
        current_ = slot;
        return true;
    }

    const std::string url_;
    mutable std::mutex mutex_;
    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> context_;
    ComPtr<ID3D11Query> done_;
    ComPtr<IMFDXGIDeviceManager> manager_;
    ComPtr<IMFMediaEngine> engine_;
    std::atomic<IMFMediaEngine*> engine_raw_{nullptr}; // set once; for the event callback
    bool failed_ = false;
    bool want_playing_ = false;
    std::optional<double> pending_seek_;
    std::array<Slot, kRing> ring_{};
    std::uint32_t ring_width_ = 0;
    std::uint32_t ring_height_ = 0;
    int current_ = -1;
    // Without texture sharing: frames go through memory.
    bool shares_textures_ = true;
    ComPtr<ID3D11Texture2D> staging_;
    std::shared_ptr<const std::vector<std::uint8_t>> pixels_;
    std::uint64_t frame_id_ = 0;
};

STDMETHODIMP EngineNotify::EventNotify(DWORD event, DWORD_PTR param1, DWORD) {
    if (const auto source = source_.lock()) {
        source->on_event(event, param1);
    }
    return S_OK;
}

#endif

} // namespace

std::shared_ptr<VideoSource> VideoSource::create(std::string url) {
#if defined(_WIN32)
    return std::make_shared<MediaEngineSource>(std::move(url));
#elif defined(__APPLE__)
    return create_avfoundation_source(std::move(url));
#else
    (void)url;
    return unsupported("video playback isn't available on this platform yet");
#endif
}

std::shared_ptr<VideoSource> VideoSource::unsupported(std::string message) {
    return std::make_shared<UnsupportedVideoSource>(std::move(message));
}

} // namespace elg::detail
