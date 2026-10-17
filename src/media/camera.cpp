// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Cameras (design.md §13.3). Windows: Media Foundation's source reader (or DirectShow, for
// cameras only it sees) on a capture thread. Frames stay in the camera's format (NV12, YUY2,
// I420 or BGRA) and go to the renderer as VideoFrame::Kind::cpu, which converts them on the GPU
// in a Vulkan pass, the same on every platform.

#include <elg/capture.h>

#include "core/log.h"
#include "core/ui_tasks.h"
#include "media/video_source.h"
#include "view/view_node.h"

#include <elg/error.h>

#include <atomic>
#include <cstring>
#include <format>
#include <mutex>
#include <thread>

#if defined(_WIN32)
#include <windows.h>
#include <dshow.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wincodec.h>
#include <wrl/client.h>
#endif

namespace elg::detail {

namespace {

#if defined(_WIN32)

using Microsoft::WRL::ComPtr;

// Media Foundation's "first video stream" pseudo-index, as the DWORD its methods take.
constexpr DWORD kVideoStream = static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM);

std::string narrow(const wchar_t* text, UINT32 length) {
    const int n = WideCharToMultiByte(CP_UTF8, 0, text, static_cast<int>(length), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(std::max(n, 0)), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, static_cast<int>(length), out.data(), n, nullptr, nullptr);
    return out;
}

std::wstring widen(const std::string& text) {
    std::wstring out(text.size(), L'\0');
    const int n = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(),
                                      static_cast<int>(out.size()));
    out.resize(static_cast<std::size_t>(std::max(n, 0)));
    return out;
}

void start_mf() {
    static const bool started = [] {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        return SUCCEEDED(MFStartup(MF_VERSION, MFSTARTUP_FULL));
    }();
    (void)started;
}

// --- DirectShow, for cameras Media Foundation doesn't see (virtual cameras on Windows 10: OBS,
// Snap Camera, ...). The Sample Grabber and Null Renderer filters still ship in qedit.dll; their
// declarations left the SDK with qedit.h, so they're declared here.

constexpr CLSID kSampleGrabber = {0xC1F400A0, 0x3F08, 0x11d3, {0x9F, 0x0B, 0x00, 0x60, 0x08, 0x03, 0x9E, 0x37}};
constexpr CLSID kNullRenderer = {0xC1F400A4, 0x3F08, 0x11d3, {0x9F, 0x0B, 0x00, 0x60, 0x08, 0x03, 0x9E, 0x37}};

MIDL_INTERFACE("0579154A-2B53-4994-B0D0-E773148EFF85")
ISampleGrabberCB : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE SampleCB(double time, IMediaSample* sample) = 0;
    virtual HRESULT STDMETHODCALLTYPE BufferCB(double time, BYTE* buffer, long length) = 0;
};

MIDL_INTERFACE("6B652FFF-11FE-4fce-92AD-0266B5D7C78F")
ISampleGrabber : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE SetOneShot(BOOL one_shot) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetMediaType(const AM_MEDIA_TYPE* type) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetConnectedMediaType(AM_MEDIA_TYPE* type) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetBufferSamples(BOOL buffer) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCurrentBuffer(long* size, long* buffer) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCurrentSample(IMediaSample** sample) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetCallback(ISampleGrabberCB* callback, long which) = 0;
};

constexpr const char* kDirectShowPrefix = "dshow:";
// FOURCC 'I420' (the SDK names only IYUV, its twin).
constexpr GUID kSubtypeI420 = {0x30323449, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71}};

// Formats taken straight from the camera (forcing RGB32 makes DirectShow insert converters that
// some cameras, OBS's among them, never feed). Better ranks first.
int format_rank(const GUID& subtype) {
    if (subtype == MEDIASUBTYPE_RGB32) {
        return 0;
    }
    if (subtype == MEDIASUBTYPE_NV12) {
        return 1;
    }
    if (subtype == MEDIASUBTYPE_YUY2) {
        return 2;
    }
    if (subtype == MEDIASUBTYPE_IYUV || subtype == kSubtypeI420) {
        return 3;
    }
    return -1; // not one we convert
}

// The frame format for a camera subtype (format_rank's).
VideoFrame::Format frame_format(const GUID& subtype) {
    if (subtype == MEDIASUBTYPE_NV12) {
        return VideoFrame::Format::nv12;
    }
    if (subtype == MEDIASUBTYPE_YUY2) {
        return VideoFrame::Format::yuy2;
    }
    if (subtype == MEDIASUBTYPE_IYUV || subtype == kSubtypeI420) {
        return VideoFrame::Format::i420;
    }
    return VideoFrame::Format::bgra;
}

// Limited-range YUV to BGRA: BT.709 for HD frames, BT.601 below.
struct YuvToRgb {
    int rv, gu, gv, bu;
    explicit YuvToRgb(int height) {
        if (height >= 720) {
            rv = 1836, gu = -218, gv = -546, bu = 2163; // x1024
        } else {
            rv = 1634, gu = -401, gv = -832, bu = 2066;
        }
    }
    void operator()(int y, int u, int v, std::uint8_t* out) const {
        const int c = 1192 * (y - 16); // 1.164 x1024
        const int d = u - 128;
        const int e = v - 128;
        const auto clamp = [](int value) { return static_cast<std::uint8_t>(std::clamp(value >> 10, 0, 255)); };
        out[0] = clamp(c + bu * d);
        out[1] = clamp(c + gu * d + gv * e);
        out[2] = clamp(c + rv * e);
        out[3] = 255;
    }
};

// A camera frame in `subtype` to top-down BGRA; false if the buffer is too short.
bool to_bgra(const GUID& subtype, const BYTE* data, long length, int width, int height, std::vector<std::uint8_t>& out) {
    const std::size_t pixels = static_cast<std::size_t>(width) * height;
    out.resize(pixels * 4);
    const YuvToRgb convert(height);
    if (subtype == MEDIASUBTYPE_NV12) {
        if (length < static_cast<long>(pixels * 3 / 2)) {
            return false;
        }
        const BYTE* uv = data + pixels;
        for (int y = 0; y < height; ++y) {
            const BYTE* row = data + static_cast<std::size_t>(y) * width;
            const BYTE* chroma = uv + static_cast<std::size_t>(y / 2) * width;
            std::uint8_t* dst = out.data() + static_cast<std::size_t>(y) * width * 4;
            for (int x = 0; x < width; ++x) {
                convert(row[x], chroma[x & ~1], chroma[(x & ~1) + 1], dst + x * 4);
            }
        }
        return true;
    }
    if (subtype == MEDIASUBTYPE_YUY2) {
        if (length < static_cast<long>(pixels * 2)) {
            return false;
        }
        for (int y = 0; y < height; ++y) {
            const BYTE* row = data + static_cast<std::size_t>(y) * width * 2;
            std::uint8_t* dst = out.data() + static_cast<std::size_t>(y) * width * 4;
            for (int x = 0; x + 1 < width; x += 2) {
                const BYTE* p = row + x * 2; // Y0 U Y1 V
                convert(p[0], p[1], p[3], dst + x * 4);
                convert(p[2], p[1], p[3], dst + (x + 1) * 4);
            }
        }
        return true;
    }
    if (subtype == MEDIASUBTYPE_IYUV || subtype == kSubtypeI420) {
        if (length < static_cast<long>(pixels * 3 / 2)) {
            return false;
        }
        const BYTE* u_plane = data + pixels;
        const BYTE* v_plane = u_plane + pixels / 4;
        for (int y = 0; y < height; ++y) {
            const BYTE* row = data + static_cast<std::size_t>(y) * width;
            const std::size_t chroma = static_cast<std::size_t>(y / 2) * (width / 2);
            std::uint8_t* dst = out.data() + static_cast<std::size_t>(y) * width * 4;
            for (int x = 0; x < width; ++x) {
                convert(row[x], u_plane[chroma + x / 2], v_plane[chroma + x / 2], dst + x * 4);
            }
        }
        return true;
    }
    return false;
}

void free_media_type(AM_MEDIA_TYPE& type) {
    if (type.cbFormat) {
        CoTaskMemFree(type.pbFormat);
    }
    if (type.pUnk) {
        type.pUnk->Release();
    }
    type = {};
}

// DirectShow's video capture devices: their names and monikers.
std::vector<std::pair<std::string, ComPtr<IMoniker>>> enumerate_directshow() {
    std::vector<std::pair<std::string, ComPtr<IMoniker>>> devices;
    ComPtr<ICreateDevEnum> enumerator;
    ComPtr<IEnumMoniker> monikers;
    if (FAILED(CoCreateInstance(CLSID_SystemDeviceEnum, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&enumerator))) ||
        enumerator->CreateClassEnumerator(CLSID_VideoInputDeviceCategory, &monikers, 0) != S_OK) {
        return devices;
    }
    ComPtr<IMoniker> moniker;
    while (monikers->Next(1, &moniker, nullptr) == S_OK) {
        ComPtr<IPropertyBag> properties;
        if (SUCCEEDED(moniker->BindToStorage(nullptr, nullptr, IID_PPV_ARGS(&properties)))) {
            VARIANT name;
            VariantInit(&name);
            if (SUCCEEDED(properties->Read(L"FriendlyName", &name, nullptr)) && name.vt == VT_BSTR) {
                devices.emplace_back(narrow(name.bstrVal, SysStringLen(name.bstrVal)), moniker);
            }
            VariantClear(&name);
        }
        moniker.Reset();
    }
    return devices;
}

// The cameras Media Foundation sees, as activation objects.
std::vector<ComPtr<IMFActivate>> enumerate_cameras() {
    start_mf();
    std::vector<ComPtr<IMFActivate>> cameras;
    ComPtr<IMFAttributes> attributes;
    if (FAILED(MFCreateAttributes(&attributes, 1)) ||
        FAILED(attributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID))) {
        return cameras;
    }
    IMFActivate** devices = nullptr;
    UINT32 count = 0;
    if (FAILED(MFEnumDeviceSources(attributes.Get(), &devices, &count))) {
        return cameras;
    }
    for (UINT32 i = 0; i < count; ++i) {
        cameras.emplace_back(devices[i]);
        devices[i]->Release();
    }
    CoTaskMemFree(devices);
    return cameras;
}

std::string string_attribute(IMFActivate* device, const GUID& key) {
    wchar_t* value = nullptr;
    UINT32 length = 0;
    if (FAILED(device->GetAllocatedString(key, &value, &length))) {
        return {};
    }
    std::string out = narrow(value, length);
    CoTaskMemFree(value);
    return out;
}

// One camera, captured on its own thread; frames reach the GPU through update().
class CameraSource final : public VideoSource {
public:
    explicit CameraSource(CameraOptions options) : options_(std::move(options)) {}

    ~CameraSource() override { stop_capture(); }

    // --- UI thread ---

    void play() override {
        if (thread_.joinable()) {
            return;
        }
        error = std::string();
        playing = true;
        thread_ = std::jthread([this, weak = std::weak_ptr<VideoSource>(shared_from_this())](std::stop_token stop) {
            capture(stop, weak);
        });
    }
    void pause() override {
        stop_capture();
        playing = false;
    }
    void seek(double) override {}
    void set_volume(float) override {}
    void set_muted(bool) override {}
    void set_looping(bool) override {}

    std::optional<std::vector<unsigned char>> snapshot() const {
        std::shared_ptr<const std::vector<std::uint8_t>> frame;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        GUID subtype = MEDIASUBTYPE_RGB32;
        {
            std::lock_guard lock(frame_mutex_);
            if (!frame_) {
                return std::nullopt;
            }
            frame = frame_;
            width = frame_width_;
            height = frame_height_;
            subtype = frame_subtype_;
        }
        std::vector<std::uint8_t> bgra;
        if (subtype == MEDIASUBTYPE_RGB32) {
            bgra = *frame;
            for (std::size_t i = 3; i < bgra.size(); i += 4) {
                bgra[i] = 255; // RGB32 leaves alpha undefined
            }
        } else if (!to_bgra(subtype, frame->data(), static_cast<long>(frame->size()), static_cast<int>(width),
                            static_cast<int>(height), bgra)) {
            return std::nullopt;
        }
        return encode_png(bgra, width, height);
    }

    // --- Render thread ---

    bool update() override {
        std::lock_guard lock(frame_mutex_);
        if (frame_number_ == reported_) {
            return false;
        }
        reported_ = frame_number_;
        return true;
    }

    std::optional<VideoFrame> current() const override {
        std::lock_guard lock(frame_mutex_);
        if (!frame_) {
            return std::nullopt;
        }
        VideoFrame frame;
        frame.kind = VideoFrame::Kind::cpu;
        frame.width = frame_width_;
        frame.height = frame_height_;
        frame.format = frame_format(frame_subtype_);
        frame.matrix = frame_height_ >= 720 ? VideoFrame::Matrix::bt709 : VideoFrame::Matrix::bt601;
        frame.pixels = frame_;
        frame.source_id = id;
        frame.frame_id = frame_number_;
        return frame;
    }

private:
    void stop_capture() {
        if (thread_.joinable()) {
            thread_.request_stop();
            thread_.join(); // ReadSample returns within a frame
        }
        playing_now_ = false;
    }

    void fail(const std::weak_ptr<VideoSource>& weak, std::string message) {
        log_warning("Camera: {}", message);
        playing_now_ = false;
        post_ui_task([weak, message] {
            if (const auto source = weak.lock()) {
                source->error = message;
                source->playing = false;
            }
        });
    }

    // The capture thread.
    void capture(std::stop_token stop, std::weak_ptr<VideoSource> weak) {
        // A single-threaded apartment with a message loop (capture_directshow): DirectShow filters
        // that are apartment-threaded, like OBS's virtual camera, only deliver frames then.
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        start_mf();
        std::vector<ComPtr<IMFActivate>> cameras = enumerate_cameras();
        // DirectShow: asked for by id, or the only cameras there are.
        if (options_.device_id.starts_with(kDirectShowPrefix) || (options_.device_id.empty() && cameras.empty())) {
            capture_directshow(stop, weak);
            return;
        }
        IMFActivate* chosen = nullptr;
        for (const auto& camera : cameras) {
            if (options_.device_id.empty() ||
                string_attribute(camera.Get(), MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK) == options_.device_id) {
                chosen = camera.Get();
                break;
            }
        }
        if (!chosen) {
            fail(weak, cameras.empty() ? "no camera is connected" : "that camera isn't connected");
            return;
        }
        ComPtr<IMFMediaSource> source;
        HRESULT result = chosen->ActivateObject(IID_PPV_ARGS(&source));
        if (FAILED(result)) {
            fail(weak, result == E_ACCESSDENIED ? "camera access is turned off in Settings > Privacy"
                                                : "the camera couldn't be opened (is another app using it?)");
            return;
        }
        ComPtr<IMFAttributes> attributes;
        MFCreateAttributes(&attributes, 1);
        attributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE); // converts formats for us
        ComPtr<IMFSourceReader> reader;
        if (FAILED(MFCreateSourceReaderFromMediaSource(source.Get(), attributes.Get(), &reader))) {
            source->Shutdown();
            fail(weak, "the camera's video couldn't be read");
            return;
        }
        choose_mode(reader.Get());
        // NV12, which most cameras send as is (the renderer converts it), else RGB32.
        bool output_set = false;
        for (const GUID& subtype : {MFVideoFormat_NV12, MFVideoFormat_RGB32}) {
            ComPtr<IMFMediaType> output;
            MFCreateMediaType(&output);
            output->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
            output->SetGUID(MF_MT_SUBTYPE, subtype);
            if (SUCCEEDED(reader->SetCurrentMediaType(kVideoStream, nullptr, output.Get()))) {
                output_set = true;
                break;
            }
        }
        if (!output_set) {
            source->Shutdown();
            fail(weak, "the camera's format can't be converted");
            return;
        }
        ComPtr<IMFMediaType> current;
        reader->GetCurrentMediaType(kVideoStream, &current);
        UINT32 width = 0;
        UINT32 height = 0;
        MFGetAttributeSize(current.Get(), MF_MT_FRAME_SIZE, &width, &height);
        GUID subtype = MFVideoFormat_RGB32;
        current->GetGUID(MF_MT_SUBTYPE, &subtype);
        post_ui_task([weak, width, height] {
            if (const auto self = weak.lock()) {
                self->size = Size{static_cast<float>(width), static_cast<float>(height)};
                self->ready = true;
            }
        });
        playing_now_ = true;

        while (!stop.stop_requested()) {
            DWORD flags = 0;
            LONGLONG timestamp = 0;
            ComPtr<IMFSample> sample;
            if (FAILED(reader->ReadSample(kVideoStream, 0, nullptr, &flags, &timestamp, &sample))) {
                fail(weak, "the camera stopped");
                break;
            }
            if (flags & (MF_SOURCE_READERF_ENDOFSTREAM | MF_SOURCE_READERF_ERROR)) {
                fail(weak, "the camera stopped");
                break;
            }
            if (!sample) {
                continue;
            }
            ComPtr<IMFMediaBuffer> buffer;
            if (FAILED(sample->ConvertToContiguousBuffer(&buffer))) {
                continue;
            }
            store(buffer.Get(), subtype, width, height);
        }
        playing_now_ = false;
        reader.Reset();
        source->Shutdown();
    }

    // The same through a DirectShow graph: camera -> Sample Grabber (RGB32) -> Null Renderer.
    void capture_directshow(std::stop_token stop, const std::weak_ptr<VideoSource>& weak) {
        const auto devices = enumerate_directshow();
        const std::string wanted = options_.device_id.starts_with(kDirectShowPrefix)
                                       ? options_.device_id.substr(std::string_view(kDirectShowPrefix).size())
                                       : std::string();
        IMoniker* moniker = nullptr;
        for (const auto& [name, device] : devices) {
            if (wanted.empty() || name == wanted) {
                moniker = device.Get();
                break;
            }
        }
        if (!moniker) {
            fail(weak, devices.empty() ? "no camera is connected" : "that camera isn't connected");
            return;
        }
        ComPtr<IBaseFilter> source;
        ComPtr<IGraphBuilder> graph;
        ComPtr<ICaptureGraphBuilder2> builder;
        ComPtr<IBaseFilter> grabber_filter;
        ComPtr<ISampleGrabber> grabber;
        ComPtr<IBaseFilter> sink;
        if (FAILED(moniker->BindToObject(nullptr, nullptr, IID_PPV_ARGS(&source))) ||
            FAILED(CoCreateInstance(CLSID_FilterGraph, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&graph))) ||
            FAILED(CoCreateInstance(CLSID_CaptureGraphBuilder2, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&builder))) ||
            FAILED(builder->SetFiltergraph(graph.Get())) ||
            FAILED(CoCreateInstance(kSampleGrabber, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&grabber_filter))) ||
            FAILED(grabber_filter.As(&grabber)) ||
            FAILED(CoCreateInstance(kNullRenderer, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&sink)))) {
            fail(weak, "the camera couldn't be opened");
            return;
        }
        graph->AddFilter(source.Get(), L"Camera");
        graph->AddFilter(grabber_filter.Get(), L"Grabber");
        graph->AddFilter(sink.Get(), L"Sink");

        // The camera's closest format to the requested size, in a format we can take as is.
        GUID chosen_subtype = MEDIASUBTYPE_RGB32;
        ComPtr<IAMStreamConfig> config;
        if (SUCCEEDED(builder->FindInterface(&PIN_CATEGORY_CAPTURE, &MEDIATYPE_Video, source.Get(), IID_PPV_ARGS(&config)))) {
            int count = 0;
            int caps_size = 0;
            config->GetNumberOfCapabilities(&count, &caps_size);
            std::vector<BYTE> caps(static_cast<std::size_t>(std::max(caps_size, 0)));
            AM_MEDIA_TYPE* best = nullptr;
            double best_score = 1e300;
            for (int i = 0; i < count; ++i) {
                AM_MEDIA_TYPE* type = nullptr;
                if (FAILED(config->GetStreamCaps(i, &type, caps.data()))) {
                    continue;
                }
                double score = 1e300;
                const int rank = format_rank(type->subtype);
                if (rank >= 0 && type->formattype == FORMAT_VideoInfo && type->pbFormat) {
                    const auto* info = reinterpret_cast<const VIDEOINFOHEADER*>(type->pbFormat);
                    const double w = info->bmiHeader.biWidth;
                    const double h = std::abs(info->bmiHeader.biHeight);
                    score = std::abs(w * h - static_cast<double>(options_.width) * options_.height) / 1e4 +
                            (w < options_.width ? 1e3 : 0) + rank;
                }
                if (score < best_score) {
                    if (best) {
                        free_media_type(*best);
                        CoTaskMemFree(best);
                    }
                    best = type;
                    best_score = score;
                } else {
                    free_media_type(*type);
                    CoTaskMemFree(type);
                }
            }
            if (best) {
                config->SetFormat(best);
                chosen_subtype = best->subtype;
                free_media_type(*best);
                CoTaskMemFree(best);
            }
        }

        AM_MEDIA_TYPE want{};
        want.majortype = MEDIATYPE_Video;
        want.subtype = chosen_subtype;
        want.formattype = FORMAT_VideoInfo;
        grabber->SetMediaType(&want);
        grabber->SetOneShot(FALSE);
        grabber->SetBufferSamples(FALSE);
        if (FAILED(builder->RenderStream(&PIN_CATEGORY_CAPTURE, &MEDIATYPE_Video, source.Get(), grabber_filter.Get(),
                                         sink.Get()))) {
            fail(weak, "the camera's video can't be converted");
            return;
        }
        AM_MEDIA_TYPE connected{};
        grabber->GetConnectedMediaType(&connected);
        const GUID subtype = connected.subtype;
        LONG width = 0;
        LONG height = 0;
        if (connected.formattype == FORMAT_VideoInfo && connected.pbFormat) {
            const auto* info = reinterpret_cast<const VIDEOINFOHEADER*>(connected.pbFormat);
            width = info->bmiHeader.biWidth;
            height = info->bmiHeader.biHeight; // > 0: bottom-up rows
        }
        free_media_type(connected);
        if (width <= 0 || height == 0) {
            fail(weak, "the camera didn't say its frame size");
            return;
        }

        // Frames arrive on DirectShow's streaming thread.
        struct Callback final : ISampleGrabberCB {
            CameraSource* camera;
            LONG width;
            LONG height;
            GUID subtype;
            std::vector<std::uint8_t> pixels;
            STDMETHODIMP QueryInterface(REFIID riid, void** object) override {
                if (riid == __uuidof(IUnknown) || riid == __uuidof(ISampleGrabberCB)) {
                    *object = static_cast<ISampleGrabberCB*>(this);
                    return S_OK;
                }
                *object = nullptr;
                return E_NOINTERFACE;
            }
            STDMETHODIMP_(ULONG) AddRef() override { return 2; } // lives on the capture thread's stack
            STDMETHODIMP_(ULONG) Release() override { return 1; }
            STDMETHODIMP SampleCB(double, IMediaSample*) override { return S_OK; }
            STDMETHODIMP BufferCB(double, BYTE* buffer, long length) override {
                const LONG rows = std::abs(height);
                if (subtype != MEDIASUBTYPE_RGB32) { // YUV is always stored top down, packed
                    camera->publish_raw(buffer, length, subtype, static_cast<UINT32>(width), static_cast<UINT32>(rows));
                    return S_OK;
                }
                if (length < width * rows * 4) {
                    return S_OK;
                }
                const LONG pitch = height > 0 ? -width * 4 : width * 4;
                const BYTE* first = height > 0 ? buffer + static_cast<std::size_t>(rows - 1) * width * 4 : buffer;
                camera->publish(first, pitch, static_cast<UINT32>(width), static_cast<UINT32>(rows));
                return S_OK;
            }
        } callback;
        callback.camera = this;
        callback.width = width;
        callback.height = height;
        callback.subtype = subtype;
        grabber->SetCallback(&callback, 1);

        // No reference clock: frames go through as they come, instead of the Null Renderer
        // holding each until its presentation time (which stalls some cameras' streams).
        if (ComPtr<IMediaFilter> filter; SUCCEEDED(graph.As(&filter))) {
            filter->SetSyncSource(nullptr);
        }
        ComPtr<IMediaControl> control;
        graph.As(&control);
        if (!control || FAILED(control->Run())) {
            grabber->SetCallback(nullptr, 1);
            fail(weak, "the camera couldn't start (is another app using it?)");
            return;
        }
        post_ui_task([weak, width, height] {
            if (const auto self = weak.lock()) {
                self->size = Size{static_cast<float>(width), static_cast<float>(std::abs(height))};
                self->ready = true;
            }
        });
        playing_now_ = true;
        while (!stop.stop_requested()) {
            // Pump this apartment's messages while waiting.
            MsgWaitForMultipleObjects(0, nullptr, FALSE, 50, QS_ALLINPUT);
            MSG message;
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }
        control->Stop();
        grabber->SetCallback(nullptr, 1);
        playing_now_ = false;
    }

    // The camera mode closest to the requested size (larger preferred over smaller), at its best
    // frame rate.
    void choose_mode(IMFSourceReader* reader) const {
        ComPtr<IMFMediaType> best;
        double best_score = 1e300;
        for (DWORD i = 0;; ++i) {
            ComPtr<IMFMediaType> type;
            if (FAILED(reader->GetNativeMediaType(kVideoStream, i, &type))) {
                break;
            }
            UINT32 w = 0;
            UINT32 h = 0;
            UINT32 num = 0;
            UINT32 den = 1;
            MFGetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, &w, &h);
            MFGetAttributeRatio(type.Get(), MF_MT_FRAME_RATE, &num, &den);
            const double fps = den ? static_cast<double>(num) / den : 0;
            const double size_error = std::abs(static_cast<double>(w) * h - static_cast<double>(options_.width) * options_.height);
            const double score = size_error / 1e4 + (w < options_.width ? 1e3 : 0) - fps;
            if (score < best_score) {
                best_score = score;
                best = type;
            }
        }
        if (best) {
            reader->SetCurrentMediaType(kVideoStream, nullptr, best.Get());
        }
    }

    // A Media Foundation frame (NV12 or RGB32), its rows packed.
    void store(IMFMediaBuffer* buffer, const GUID& subtype, UINT32 width, UINT32 height) {
        const bool nv12 = subtype == MFVideoFormat_NV12;
        BYTE* data = nullptr;
        LONG pitch = static_cast<LONG>(nv12 ? width : width * 4);
        ComPtr<IMF2DBuffer> buffer2d;
        bool locked2d = false;
        if (SUCCEEDED(buffer->QueryInterface(IID_PPV_ARGS(&buffer2d))) && SUCCEEDED(buffer2d->Lock2D(&data, &pitch))) {
            locked2d = true;
        } else {
            DWORD length = 0;
            if (FAILED(buffer->Lock(&data, nullptr, &length))) {
                return;
            }
        }
        if (nv12) {
            publish_nv12(data, pitch, width, height);
        } else {
            publish(data, pitch, width, height);
        }
        locked2d ? buffer2d->Unlock2D() : buffer->Unlock();
    }

public:
    // A BGRA frame whose first (top) row is at `first`, rows `pitch` bytes apart (negative:
    // stored bottom-up). Any capture thread.
    void publish(const BYTE* first, LONG pitch, UINT32 width, UINT32 height) {
        auto pixels = std::make_shared<std::vector<std::uint8_t>>(static_cast<std::size_t>(width) * height * 4);
        for (UINT32 y = 0; y < height; ++y) {
            std::memcpy(pixels->data() + static_cast<std::size_t>(y) * width * 4,
                        first + static_cast<std::ptrdiff_t>(y) * pitch, static_cast<std::size_t>(width) * 4);
        }
        set_frame(std::move(pixels), MEDIASUBTYPE_RGB32, width, height);
    }

    // An NV12 frame whose rows are `pitch` bytes apart, the chroma plane right after the luma.
    void publish_nv12(const BYTE* data, LONG pitch, UINT32 width, UINT32 height) {
        auto pixels = std::make_shared<std::vector<std::uint8_t>>(packed_size(VideoFrame::Format::nv12, width, height));
        const std::size_t chroma_row = (width + 1) / 2 * 2;
        std::uint8_t* out = pixels->data();
        for (UINT32 y = 0; y < height; ++y, out += width) {
            std::memcpy(out, data + static_cast<std::size_t>(y) * pitch, width);
        }
        const BYTE* chroma = data + static_cast<std::size_t>(pitch) * height;
        for (UINT32 y = 0; y < (height + 1) / 2; ++y, out += chroma_row) {
            std::memcpy(out, chroma + static_cast<std::size_t>(y) * pitch, chroma_row);
        }
        set_frame(std::move(pixels), MEDIASUBTYPE_NV12, width, height);
    }

    // A packed frame as the camera sent it (NV12, YUY2, I420). Any capture thread.
    void publish_raw(const BYTE* data, long length, const GUID& subtype, UINT32 width, UINT32 height) {
        const std::size_t packed = packed_size(frame_format(subtype), width, height);
        if (length < 0 || static_cast<std::size_t>(length) < packed) {
            return;
        }
        set_frame(std::make_shared<std::vector<std::uint8_t>>(data, data + packed), subtype, width, height);
    }

private:
    void set_frame(std::shared_ptr<const std::vector<std::uint8_t>> pixels, const GUID& subtype, UINT32 width,
                   UINT32 height) {
        std::lock_guard lock(frame_mutex_);
        frame_ = std::move(pixels);
        frame_width_ = width;
        frame_height_ = height;
        frame_subtype_ = subtype;
        ++frame_number_;
    }


    static std::optional<std::vector<unsigned char>> encode_png(const std::vector<std::uint8_t>& bgra, UINT width, UINT height) {
        ComPtr<IWICImagingFactory> factory;
        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)))) {
            return std::nullopt;
        }
        ComPtr<IStream> stream;
        CreateStreamOnHGlobal(nullptr, TRUE, &stream);
        ComPtr<IWICBitmapEncoder> encoder;
        ComPtr<IWICBitmapFrameEncode> frame;
        if (FAILED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) ||
            FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) ||
            FAILED(encoder->CreateNewFrame(&frame, nullptr)) || FAILED(frame->Initialize(nullptr)) ||
            FAILED(frame->SetSize(width, height))) {
            return std::nullopt;
        }
        WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
        frame->SetPixelFormat(&format);
        if (FAILED(frame->WritePixels(height, width * 4, static_cast<UINT>(bgra.size()), const_cast<BYTE*>(bgra.data()))) ||
            FAILED(frame->Commit()) || FAILED(encoder->Commit())) {
            return std::nullopt;
        }
        HGLOBAL memory = nullptr;
        GetHGlobalFromStream(stream.Get(), &memory);
        STATSTG stat{};
        stream->Stat(&stat, STATFLAG_NONAME);
        const auto* bytes = static_cast<const unsigned char*>(GlobalLock(memory));
        std::vector<unsigned char> png(bytes, bytes + stat.cbSize.QuadPart);
        GlobalUnlock(memory);
        return png;
    }

    CameraOptions options_;
    std::jthread thread_;

    mutable std::mutex frame_mutex_;
    // The newest frame, in the camera's format (frame_subtype_), packed, top down. Shared with
    // the renderer, which reads it while the capture thread makes the next one.
    std::shared_ptr<const std::vector<std::uint8_t>> frame_;
    GUID frame_subtype_ = MEDIASUBTYPE_RGB32;
    std::uint32_t frame_width_ = 0;
    std::uint32_t frame_height_ = 0;
    std::uint64_t frame_number_ = 0;
    std::uint64_t reported_ = 0; // the frame update() last reported
};

#endif

} // namespace

} // namespace elg::detail

namespace elg {

std::vector<CameraDevice> camera_devices() {
    std::vector<CameraDevice> devices;
#if defined(_WIN32)
    for (const auto& camera : detail::enumerate_cameras()) {
        devices.push_back({detail::string_attribute(camera.Get(), MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK),
                           detail::string_attribute(camera.Get(), MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME)});
    }
    for (const auto& [name, moniker] : detail::enumerate_directshow()) {
        if (std::ranges::none_of(devices, [&](const CameraDevice& d) { return d.name == name; })) {
            devices.push_back({std::string(detail::kDirectShowPrefix) + name, name});
        }
    }
#endif
    return devices;
}

Camera::Camera(CameraOptions options) : mirrored_(options.mirrored) {
#if defined(_WIN32)
    source_ = std::make_shared<detail::CameraSource>(std::move(options));
#else
    source_ = detail::VideoSource::unsupported("cameras aren't available on this platform yet");
#endif
}

void Camera::start() const {
    source_->play();
}

void Camera::stop() const {
    source_->pause();
}

bool Camera::is_running() const {
    return source_->playing();
}

Size Camera::size() const {
    return source_->size();
}

std::string Camera::error() const {
    return source_->error();
}

std::optional<std::vector<unsigned char>> Camera::snapshot() const {
#if defined(_WIN32)
    return static_cast<const detail::CameraSource&>(*source_).snapshot();
#else
    return std::nullopt;
#endif
}

} // namespace elg
