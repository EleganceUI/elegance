// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Images on Windows (media/image_decode.h): the Windows Imaging Component.

#include "media/image_decode.h"

#include <elg/error.h>

#include <algorithm>
#include <format>
#include <string_view>

#include <windows.h>
#include <propvarutil.h>
#include <wincodec.h>
#include <wrl/client.h>

namespace elg::detail {

namespace {

using Microsoft::WRL::ComPtr;

void check_hr(HRESULT result, std::string_view what) {
    if (FAILED(result)) {
        throw ResourceError(std::format("can't {} (HRESULT 0x{:08x})", what, static_cast<unsigned long>(result)));
    }
}

// WIC is apartment-agnostic for decoding; each thread initializes COM once.
IWICImagingFactory* factory() {
    thread_local ComPtr<IWICImagingFactory> instance = [] {
        const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        (void)com; // S_FALSE / RPC_E_CHANGED_MODE: COM is already set up on this thread
        ComPtr<IWICImagingFactory> created;
        check_hr(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&created)),
                 "start the Windows Imaging Component");
        return created;
    }();
    return instance.Get();
}

ComPtr<IWICBitmapFrameDecode> first_frame(std::span<const std::uint8_t> data) {
    ComPtr<IWICStream> stream;
    check_hr(factory()->CreateStream(&stream), "create an image stream");
    check_hr(stream->InitializeFromMemory(const_cast<BYTE*>(data.data()), static_cast<DWORD>(data.size())),
             "read the image");
    ComPtr<IWICBitmapDecoder> decoder;
    const HRESULT found = factory()->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &decoder);
    if (FAILED(found)) {
        throw ResourceError("not an image format this system can decode");
    }
    ComPtr<IWICBitmapFrameDecode> frame;
    check_hr(decoder->GetFrame(0, &frame), "read the image's first frame");
    return frame;
}

// The EXIF orientation (1 when absent): JPEG and TIFF keep it in different places.
int exif_orientation(IWICBitmapFrameDecode* frame) {
    ComPtr<IWICMetadataQueryReader> reader;
    if (FAILED(frame->GetMetadataQueryReader(&reader))) {
        return 1;
    }
    for (const wchar_t* query : {L"/app1/ifd/{ushort=274}", L"/ifd/{ushort=274}"}) {
        PROPVARIANT value;
        PropVariantInit(&value);
        if (SUCCEEDED(reader->GetMetadataByName(query, &value))) {
            const int orientation = value.vt == VT_UI2 ? value.uiVal : 1;
            PropVariantClear(&value);
            return orientation;
        }
    }
    return 1;
}

} // namespace

StoredImageInfo platform_probe_image(std::span<const std::uint8_t> data) {
    const ComPtr<IWICBitmapFrameDecode> frame = first_frame(data);
    UINT width = 0;
    UINT height = 0;
    check_hr(frame->GetSize(&width, &height), "read the image's size");
    return {width, height, exif_orientation(frame.Get())};
}

int platform_decode_image(std::span<const std::uint8_t> data, std::uint32_t max_size, DecodedImage& image) {
    const ComPtr<IWICBitmapFrameDecode> frame = first_frame(data);
    const int orientation = exif_orientation(frame.Get());
    UINT width = 0;
    UINT height = 0;
    check_hr(frame->GetSize(&width, &height), "read the image's size");
    ComPtr<IWICBitmapSource> source = frame;
    if (width > max_size || height > max_size) {
        const double k = static_cast<double>(max_size) / std::max(width, height);
        const UINT scaled_w = std::max(1u, static_cast<UINT>(width * k));
        const UINT scaled_h = std::max(1u, static_cast<UINT>(height * k));
        ComPtr<IWICBitmapScaler> scaler;
        check_hr(factory()->CreateBitmapScaler(&scaler), "create an image scaler");
        check_hr(scaler->Initialize(source.Get(), scaled_w, scaled_h, WICBitmapInterpolationModeFant), "scale the image");
        source = scaler;
        width = scaled_w;
        height = scaled_h;
    }
    ComPtr<IWICFormatConverter> converter;
    check_hr(factory()->CreateFormatConverter(&converter), "create an image converter");
    check_hr(converter->Initialize(source.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0,
                                   WICBitmapPaletteTypeCustom),
             "convert the image to RGBA");
    image.width = width;
    image.height = height;
    image.rgba.resize(static_cast<std::size_t>(width) * height * 4);
    check_hr(converter->CopyPixels(nullptr, width * 4, static_cast<UINT>(image.rgba.size()), image.rgba.data()),
             "decode the image");
    return orientation;
}

std::optional<std::vector<unsigned char>> encode_png(std::span<const std::uint8_t> bgra, std::uint32_t width,
                                                     std::uint32_t height) {
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

} // namespace elg::detail
