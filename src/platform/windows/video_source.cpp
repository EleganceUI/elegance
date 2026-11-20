// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Videos on Windows (design.md §13.3, media/video_backend.h): Media Foundation's media engine in
// frame-server mode, rendering each new frame into a ring of D3D11 textures shared with WebGPU.

#include "media/video_backend.h"
#include "platform/windows/com.h"

#include "core/log.h"
#include "core/profile.h"
#include "core/threads.h"
#include "core/ui_tasks.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <format>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>
#include <windows.h>
#include <d3d11_4.h>
#include <dxgi1_4.h>
#include <mfapi.h>
#include <mfmediaengine.h>
#include <wrl/client.h>

namespace elg::detail {

namespace {

using Microsoft::WRL::ComPtr;

// Media Foundation, started once per process, on whichever thread first plays a video: COM in
// that thread's apartment, or, where nothing set it up, the multithreaded one.
bool start_media_foundation() {
    static const bool started = [] {
        return SUCCEEDED(os::keep_multithreaded_apartment()) && SUCCEEDED(MFStartup(MF_VERSION, MFSTARTUP_FULL));
    }();
    return started;
}

std::string hr_text(HRESULT result) {
    return std::format("HRESULT 0x{:08x}", static_cast<unsigned long>(result));
}

// Throws, saying `what` failed, when `result` is a failure: setting the engine up.
void check(HRESULT result, std::string_view what) {
    if (FAILED(result)) {
        throw std::runtime_error(std::format("can't {} ({})", what, hr_text(result)));
    }
}

class MediaFoundationVideo;

// The engine's event callback (on Media Foundation's threads). Holds the source weakly: while it
// lives, so does `video`.
class EngineNotify final : public IMFMediaEngineNotify {
public:
    EngineNotify(MediaFoundationVideo* video, std::weak_ptr<FrameSourceHost> host)
        : video_(video), host_(std::move(host)) {}

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
    MediaFoundationVideo* video_;
    std::weak_ptr<FrameSourceHost> host_;
};

class MediaFoundationVideo final : public VideoEngine {
public:
    explicit MediaFoundationVideo(std::string url) : url_(std::move(url)) {}
    MediaFoundationVideo(const MediaFoundationVideo&) = delete;
    MediaFoundationVideo& operator=(const MediaFoundationVideo&) = delete;
    MediaFoundationVideo(MediaFoundationVideo&&) = delete;
    MediaFoundationVideo& operator=(MediaFoundationVideo&&) = delete;

    ~MediaFoundationVideo() override {
        ticker_ = std::jthread(); // stops it, and waits: it calls into the engine
        std::lock_guard lock(mutex_);
        if (engine_) {
            if (const HRESULT result = engine_->Shutdown(); FAILED(result)) {
                log_warning("video: can't shut {} down ({})", url_, hr_text(result));
            }
            engine_raw_ = nullptr;
        }
        for (Slot& slot : ring_) {
            if (slot.handle) {
                CloseHandle(slot.handle);
            }
        }
    }

    // Its ticker wakes the render thread for each new frame (tick()).
    [[nodiscard]] bool wakes_for_frames() const noexcept override { return true; }

    // The engine itself is made once the renderer's adapter is known (ensure_engine()).
    void open(const std::shared_ptr<FrameSourceHost>& host) override {
        host_ = host.get();
        weak_host_ = host;
    }

    // --- UI thread ---

    // Each call the engine refuses is the player's error(), and leaves its state as it was.

    void play() override {
        std::lock_guard lock(mutex_);
        if (ensure_engine()) {
            HRESULT result = engine_->IsEnded() ? engine_->SetCurrentTime(0) : S_OK;
            if (SUCCEEDED(result)) {
                result = engine_->Play();
            }
            if (FAILED(result)) {
                report(result, "play");
                return;
            }
        }
        want_playing_ = true;
        host_->set_playing(true);
        host_->set_playing_now(true);
    }

    void pause() override {
        std::lock_guard lock(mutex_);
        if (ensure_engine()) {
            if (const HRESULT result = engine_->Pause(); FAILED(result)) {
                report(result, "pause");
                return;
            }
        }
        want_playing_ = false;
        host_->set_playing(false);
        host_->set_playing_now(false);
    }

    void seek(double seconds) override {
        std::lock_guard lock(mutex_);
        seconds = std::max(0.0, seconds);
        if (ensure_engine()) {
            if (const HRESULT result = engine_->SetCurrentTime(seconds); FAILED(result)) {
                report(result, "seek in");
                return;
            }
        } else {
            pending_seek_ = seconds;
        }
        host_->set_time(seconds);
    }

    void set_volume(float value) override {
        std::lock_guard lock(mutex_);
        value = std::clamp(value, 0.f, 1.f);
        if (ensure_engine()) {
            if (const HRESULT result = engine_->SetVolume(value); FAILED(result)) {
                report(result, "change the volume of");
                return;
            }
        }
        want_volume_ = value;
        host_->set_volume(value);
    }

    void set_muted(bool value) override {
        std::lock_guard lock(mutex_);
        if (ensure_engine()) {
            if (const HRESULT result = engine_->SetMuted(value); FAILED(result)) {
                report(result, value ? "mute" : "unmute");
                return;
            }
        }
        want_muted_ = value;
        host_->set_muted(value);
    }

    void set_looping(bool value) override {
        std::lock_guard lock(mutex_);
        if (ensure_engine()) {
            if (const HRESULT result = engine_->SetLoop(value); FAILED(result)) {
                report(result, "loop");
                return;
            }
        }
        want_looping_ = value;
        host_->set_looping(value);
    }

    // --- Render thread ---

    // It waits for the GPU's copy of the frame without the mutex, which the UI thread's controls
    // take (ELGSPEC-0023.R6). Only this thread uses the D3D11 context, the query and the ring,
    // which exist once the engine does.
    bool update() override {
        std::unique_lock lock(mutex_);
        if (!ensure_engine()) {
            return false;
        }
        if (!frame_ready_.exchange(false)) {
            return false; // no new frame (tick())
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
            return read_back(ring_[slot].texture.Get(), slot, lock);
        }
        // WebGPU samples it next: the copy must be done. A failed query (the device was
        // removed) shows no new frame.
        context_->End(done_.Get());
        context_->Flush();
        lock.unlock();
        HRESULT done = S_FALSE;
        while ((done = context_->GetData(done_.Get(), nullptr, 0, 0)) == S_FALSE) {
            SwitchToThread();
        }
        lock.lock();
        if (FAILED(done)) {
            return false;
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
            frame.source_id = host_->source_id();
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
        const auto post = [weak = weak_host_](auto action) {
            post_ui_task([weak, action = std::move(action)] {
                if (const auto host = weak.lock()) {
                    action(*host);
                }
            });
        };
        switch (event) {
        case MF_MEDIA_ENGINE_EVENT_LOADEDMETADATA:
        case MF_MEDIA_ENGINE_EVENT_DURATIONCHANGE: {
            const double length = engine->GetDuration();
            DWORD w = 0;
            DWORD h = 0;
            if (FAILED(engine->GetNativeVideoSize(&w, &h))) {
                w = 0; // no size yet (audio only, or not known): a later frame's size applies
                h = 0;
            }
            post([length, w, h](FrameSourceHost& host) {
                host.set_duration(std::isfinite(length) ? length : 0.0);
                host.set_size(static_cast<float>(w), static_cast<float>(h));
                host.set_ready();
            });
            break;
        }
        case MF_MEDIA_ENGINE_EVENT_TIMEUPDATE: {
            const double now = engine->GetCurrentTime();
            post([now](FrameSourceHost& host) { host.set_time(now); });
            break;
        }
        case MF_MEDIA_ENGINE_EVENT_PLAYING:
            host_->set_playing_now(true);
            post([](FrameSourceHost& host) { host.set_playing(true); });
            break;
        case MF_MEDIA_ENGINE_EVENT_PAUSE:
        case MF_MEDIA_ENGINE_EVENT_ENDED: {
            host_->set_playing_now(false);
            const double now = engine->GetCurrentTime();
            post([now](FrameSourceHost& host) {
                host.set_playing(false);
                host.set_time(now);
            });
            break;
        }
        case MF_MEDIA_ENGINE_EVENT_ERROR: {
            host_->set_playing_now(false);
            const auto code = static_cast<MF_MEDIA_ENGINE_ERR>(param1);
            std::string message = std::format("can't play {} (media error {})", url_, static_cast<int>(code));
            if (code == MF_MEDIA_ENGINE_ERR_SRC_NOT_SUPPORTED) {
                message = std::format("can't play {}: the format isn't supported, or the file is missing", url_);
            }
            log_warning("video: {}", message);
            post([message](FrameSourceHost& host) {
                host.set_error(message);
                host.set_playing(false);
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
        const std::optional<RenderAdapter> adapter = host_->render_adapter();
        if (!adapter) {
            return false; // the renderer isn't up yet: what was asked for applies once it is
        }
        shares_textures_ = adapter->shares_textures;
        try {
            create_engine(adapter->luid);
        } catch (const std::exception& failure) {
            failed_ = true;
            if (engine_) { // made, but it couldn't load the video
                engine_raw_ = nullptr;
                if (const HRESULT result = engine_->Shutdown(); FAILED(result)) {
                    log_warning("video: can't shut {} down ({})", url_, hr_text(result));
                }
                engine_.Reset();
            }
            const std::string message = failure.what();
            log_warning("video: {}", message);
            post_ui_task([weak = weak_host_, message] {
                if (const auto host = weak.lock()) {
                    host->set_error(message);
                }
            });
            return false;
        }
        // Apply what was asked for before the engine existed: from copies, since this may run on
        // the render thread, and the states belong to the UI thread. What the engine refuses is
        // the player's error.
        HRESULT result = engine_->SetVolume(want_volume_);
        if (SUCCEEDED(result)) {
            result = engine_->SetMuted(want_muted_);
        }
        if (SUCCEEDED(result)) {
            result = engine_->SetLoop(want_looping_);
        }
        if (SUCCEEDED(result) && pending_seek_) {
            result = engine_->SetCurrentTime(*pending_seek_);
        }
        pending_seek_.reset();
        if (SUCCEEDED(result) && want_playing_) {
            result = engine_->Play();
        }
        if (FAILED(result)) {
            report(result, "set up");
        }
        ticker_ = std::jthread([this](const std::stop_token& stop) { tick(stop); });
        return true;
    }

    // The ticker's thread: Media Foundation says when a new frame is due only when asked
    // (OnVideoStreamTick, which takes it), so this asks, every 4 ms while the video plays and every
    // 100 ms while it doesn't (a seek shows a frame too), and wakes the render thread only when one
    // is: the render thread doesn't look for frames itself (ELGSPEC-0027.R4). update() transfers
    // it. From the engine's creation to its destruction.
    void tick(const std::stop_token& stop) {
        ELG_THREAD_NAME("Elegance video");
        const ThreadSetup thread_setup; // Media Foundation's COM
        std::mutex sleeping;
        std::condition_variable_any wake;
        std::unique_lock lock(sleeping);
        while (!stop.stop_requested()) {
            IMFMediaEngine* const engine = engine_raw_.load();
            LONGLONG due = 0;
            if (engine && !frame_ready_.load() && engine->OnVideoStreamTick(&due) == S_OK) {
                frame_ready_ = true;
                run_foreign_callback([this] { host_->notify_new_frame(); });
            }
            const auto interval = host_->is_playing_now() ? std::chrono::milliseconds(4) : std::chrono::milliseconds(100);
            wake.wait_for(lock, stop, interval, [] { return false; });
        }
    }

    // A call into the engine failed: logged, and the player's error() on the UI thread. Any
    // thread, with the mutex held.
    void report(HRESULT result, std::string_view what) {
        const std::string message = std::format("can't {} {} ({})", what, url_, hr_text(result));
        log_warning("video: {}", message);
        post_ui_task([weak = weak_host_, message] {
            if (const auto host = weak.lock()) {
                host->set_error(message);
            }
        });
    }

    void create_engine(const std::array<std::uint8_t, 8>& adapter_luid) {
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
        auto* notify = new EngineNotify(this, weak_host_);
        const HRESULT held = attributes->SetUnknown(MF_MEDIA_ENGINE_CALLBACK, notify);
        notify->Release(); // the attributes and the engine hold it
        check(held, "hear the media engine's events");
        check(attributes->SetUnknown(MF_MEDIA_ENGINE_DXGI_MANAGER, manager_.Get()), "give the media engine the GPU");
        check(attributes->SetUINT32(MF_MEDIA_ENGINE_VIDEO_OUTPUT_FORMAT, DXGI_FORMAT_B8G8R8A8_UNORM),
              "ask the media engine for BGRA frames");
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
        check(engine_->SetPreload(MF_MEDIA_ENGINE_PRELOAD_AUTOMATIC), "preload the video");
        check(engine_->Load(), "load the video");
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
            slot.id = host_->new_id();
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

    // With `lock` (the mutex) held. The renderer can't import the texture: copies the frame into
    // memory, packed, for the renderer to upload; Map() waits for the GPU's copy, without the lock.
    bool read_back(ID3D11Texture2D* texture, int slot, std::unique_lock<std::mutex>& lock) {
        context_->CopyResource(staging_.Get(), texture);
        const std::uint32_t width = ring_width_;
        const std::uint32_t height = ring_height_;
        lock.unlock();
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(context_->Map(staging_.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
            lock.lock();
            return false;
        }
        const std::size_t row = std::size_t{width} * 4;
        auto pixels = std::make_shared<std::vector<std::uint8_t>>(row * height);
        for (std::uint32_t y = 0; y < height; ++y) {
            std::memcpy(pixels->data() + row * y, static_cast<const std::uint8_t*>(mapped.pData) + std::size_t{mapped.RowPitch} * y, row);
        }
        context_->Unmap(staging_.Get(), 0);
        lock.lock();
        pixels_ = std::move(pixels);
        ++frame_id_;
        current_ = slot;
        return true;
    }

    const std::string url_;
    FrameSourceHost* host_ = nullptr; // set by open(); outlives the engine
    std::weak_ptr<FrameSourceHost> weak_host_; // for what's posted to the UI thread
    mutable std::mutex mutex_;
    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> context_;
    ComPtr<ID3D11Query> done_;
    ComPtr<IMFDXGIDeviceManager> manager_;
    ComPtr<IMFMediaEngine> engine_;
    std::atomic<IMFMediaEngine*> engine_raw_{nullptr}; // set once; for the event callback
    std::atomic<bool> frame_ready_{false}; // tick() found a frame, which update() hasn't taken
    std::jthread ticker_;                  // tick(), once the engine exists
    bool failed_ = false;
    bool want_playing_ = false;
    float want_volume_ = 1.f;
    bool want_muted_ = false;
    bool want_looping_ = false;
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

// Media Foundation calls it, on its threads: nothing may escape it (ELGSPEC-0002.R10).
STDMETHODIMP EngineNotify::EventNotify(DWORD event, DWORD_PTR param1, DWORD) {
    run_foreign_callback([&] {
        if (const auto host = host_.lock()) {
            video_->on_event(event, param1);
        }
    });
    return S_OK;
}

} // namespace

std::expected<std::unique_ptr<VideoEngine>, std::string> create_video_engine(const std::string& url) {
    return std::make_unique<MediaFoundationVideo>(url);
}

} // namespace elg::detail
