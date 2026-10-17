// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Image decoding (design.md §13.3): the platform's codecs. Windows Imaging Component on Windows,
// ImageIO on macOS; other platforms use stb_image until their native decoders are wired up.

#include "media/image_decode.h"

#include <elg/error.h>

#include <algorithm>
#include <atomic>
#include <format>
#include <utility>

#if defined(_WIN32)
#include <windows.h>
#include <propvarutil.h>
#include <wincodec.h>
#include <wrl/client.h>
#elif defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>
#elif __has_include(<stb_image.h>)
#define ELG_HAVE_STB_IMAGE 1
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#include <stb_image.h>
#endif

namespace elg::detail {

namespace {

std::atomic<std::uint64_t> g_next_id{1};

// EXIF orientation 1-8: whether the stored pixels are turned a quarter (the upright size swaps).
bool swaps_axes(int orientation) {
    return orientation >= 5 && orientation <= 8;
}

// Applies EXIF orientation `orientation` to RGBA pixels (w x h, as stored).
void orient(DecodedImage& image, int orientation) {
    if (orientation <= 1 || orientation > 8) {
        return;
    }
    const std::uint32_t w = image.width;
    const std::uint32_t h = image.height;
    const bool swap = swaps_axes(orientation);
    const std::uint32_t out_w = swap ? h : w;
    const std::uint32_t out_h = swap ? w : h;
    std::vector<std::uint8_t> out(image.rgba.size());
    const auto* in = reinterpret_cast<const std::uint32_t*>(image.rgba.data());
    auto* dst = reinterpret_cast<std::uint32_t*>(out.data());
    for (std::uint32_t y = 0; y < out_h; ++y) {
        for (std::uint32_t x = 0; x < out_w; ++x) {
            // The stored pixel shown at (x, y).
            std::uint32_t sx = x;
            std::uint32_t sy = y;
            switch (orientation) {
            case 2: sx = w - 1 - x; sy = y; break;         // mirrored
            case 3: sx = w - 1 - x; sy = h - 1 - y; break; // upside down
            case 4: sx = x; sy = h - 1 - y; break;         // mirrored, upside down
            case 5: sx = y; sy = x; break;                 // transposed
            case 6: sx = y; sy = h - 1 - x; break;         // turned right: rotate 90 clockwise
            case 7: sx = w - 1 - y; sy = h - 1 - x; break; // transversed
            case 8: sx = w - 1 - y; sy = x; break;         // turned left: rotate 90 counter-clockwise
            default: break;
            }
            dst[y * out_w + x] = in[sy * w + sx];
        }
    }
    image.rgba = std::move(out);
    image.width = out_w;
    image.height = out_h;
}

#if defined(_WIN32)

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

#elif defined(__APPLE__)

// Owns a Core Foundation object (CFRelease on destruction).
template <typename T>
class CFRef {
public:
    explicit CFRef(T ref = nullptr) noexcept : ref_(ref) {}
    ~CFRef() {
        if (ref_) {
            CFRelease(ref_);
        }
    }
    CFRef(CFRef&& other) noexcept : ref_(std::exchange(other.ref_, nullptr)) {}
    CFRef& operator=(CFRef&& other) noexcept {
        std::swap(ref_, other.ref_);
        return *this;
    }
    T get() const noexcept { return ref_; }
    explicit operator bool() const noexcept { return ref_ != nullptr; }

private:
    T ref_;
};

CFRef<CGImageSourceRef> image_source(std::span<const std::uint8_t> data) {
    // No copy: the data outlives the source, which is released before we return.
    const CFRef<CFDataRef> bytes(CFDataCreateWithBytesNoCopy(nullptr, data.data(), static_cast<CFIndex>(data.size()),
                                                            kCFAllocatorNull));
    CFRef<CGImageSourceRef> source(bytes ? CGImageSourceCreateWithData(bytes.get(), nullptr) : nullptr);
    if (!source || CGImageSourceGetCount(source.get()) == 0) {
        throw ResourceError("not an image format this system can decode");
    }
    return source;
}

int int_property(CFDictionaryRef properties, CFStringRef key, int fallback) {
    const auto number = static_cast<CFNumberRef>(CFDictionaryGetValue(properties, key));
    int value = fallback;
    if (!number || CFGetTypeID(number) != CFNumberGetTypeID() || !CFNumberGetValue(number, kCFNumberIntType, &value)) {
        return fallback;
    }
    return value;
}

// The stored size and the EXIF orientation (1 when absent) of the first image.
struct SourceInfo {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    int orientation = 1;
};

SourceInfo source_info(CGImageSourceRef source) {
    const CFRef<CFDictionaryRef> properties(CGImageSourceCopyPropertiesAtIndex(source, 0, nullptr));
    if (!properties) {
        throw ResourceError("can't read the image's properties");
    }
    const int width = int_property(properties.get(), kCGImagePropertyPixelWidth, 0);
    const int height = int_property(properties.get(), kCGImagePropertyPixelHeight, 0);
    if (width <= 0 || height <= 0) {
        throw ResourceError("can't read the image's size");
    }
    return {static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height),
            int_property(properties.get(), kCGImagePropertyOrientation, 1)};
}

#endif

} // namespace

ImageInfo probe_image(std::span<const std::uint8_t> data) {
#if defined(_WIN32)
    const ComPtr<IWICBitmapFrameDecode> frame = first_frame(data);
    UINT width = 0;
    UINT height = 0;
    check_hr(frame->GetSize(&width, &height), "read the image's size");
    if (swaps_axes(exif_orientation(frame.Get()))) {
        std::swap(width, height);
    }
    return {width, height};
#elif defined(__APPLE__)
    const SourceInfo info = source_info(image_source(data).get());
    return swaps_axes(info.orientation) ? ImageInfo{info.height, info.width} : ImageInfo{info.width, info.height};
#elif defined(ELG_HAVE_STB_IMAGE)
    int width = 0;
    int height = 0;
    int channels = 0;
    if (!stbi_info_from_memory(data.data(), static_cast<int>(data.size()), &width, &height, &channels)) {
        throw ResourceError(std::format("can't read the image: {}", stbi_failure_reason()));
    }
    return {static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height)};
#else
    (void)data;
    throw ResourceError("this build has no image decoder");
#endif
}

std::shared_ptr<const DecodedImage> decode_image(std::span<const std::uint8_t> data, std::uint32_t max_size) {
    auto image = std::make_shared<DecodedImage>();
    image->id = g_next_id.fetch_add(1);
#if defined(_WIN32)
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
    image->width = width;
    image->height = height;
    image->rgba.resize(static_cast<std::size_t>(width) * height * 4);
    check_hr(converter->CopyPixels(nullptr, width * 4, static_cast<UINT>(image->rgba.size()), image->rgba.data()),
             "decode the image");
    orient(*image, orientation);
#elif defined(__APPLE__)
    const CFRef<CGImageSourceRef> source = image_source(data);
    const SourceInfo info = source_info(source.get());
    CFRef<CGImageRef> picture(nullptr);
    if (info.width > max_size || info.height > max_size) {
        // A thumbnail scales while decoding. It's left as stored; orient() turns it below.
        const int limit = static_cast<int>(max_size);
        const CFRef<CFNumberRef> size(CFNumberCreate(nullptr, kCFNumberIntType, &limit));
        const void* keys[] = {kCGImageSourceCreateThumbnailFromImageAlways, kCGImageSourceThumbnailMaxPixelSize,
                              kCGImageSourceCreateThumbnailWithTransform};
        const void* values[] = {kCFBooleanTrue, size.get(), kCFBooleanFalse};
        const CFRef<CFDictionaryRef> options(CFDictionaryCreate(nullptr, keys, values, 3, &kCFTypeDictionaryKeyCallBacks,
                                                                &kCFTypeDictionaryValueCallBacks));
        picture = CFRef<CGImageRef>(CGImageSourceCreateThumbnailAtIndex(source.get(), 0, options.get()));
    } else {
        picture = CFRef<CGImageRef>(CGImageSourceCreateImageAtIndex(source.get(), 0, nullptr));
    }
    if (!picture) {
        throw ResourceError("can't decode the image");
    }
    const std::size_t width = CGImageGetWidth(picture.get());
    const std::size_t height = CGImageGetHeight(picture.get());
    image->width = static_cast<std::uint32_t>(width);
    image->height = static_cast<std::uint32_t>(height);
    image->rgba.resize(width * height * 4);
    // Drawing converts from the image's color profile to sRGB. Bitmap contexts are always
    // premultiplied, so the alpha is undone afterwards.
    const CFRef<CGColorSpaceRef> srgb(CGColorSpaceCreateWithName(kCGColorSpaceSRGB));
    const CFRef<CGContextRef> context(CGBitmapContextCreate(image->rgba.data(), width, height, 8, width * 4, srgb.get(),
                                                            static_cast<std::uint32_t>(kCGImageAlphaPremultipliedLast) |
                                                                static_cast<std::uint32_t>(kCGBitmapByteOrder32Big)));
    if (!context) {
        throw ResourceError("can't decode the image");
    }
    CGContextSetBlendMode(context.get(), kCGBlendModeCopy);
    CGContextDrawImage(context.get(), CGRectMake(0, 0, static_cast<CGFloat>(width), static_cast<CGFloat>(height)),
                       picture.get());
    for (std::size_t i = 0; i < image->rgba.size(); i += 4) {
        const unsigned alpha = image->rgba[i + 3];
        if (alpha != 0 && alpha != 255) {
            for (std::size_t c = 0; c < 3; ++c) {
                image->rgba[i + c] = static_cast<std::uint8_t>(std::min(255u, (image->rgba[i + c] * 255u + alpha / 2) / alpha));
            }
        }
    }
    orient(*image, info.orientation);
#elif defined(ELG_HAVE_STB_IMAGE)
    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_uc* pixels = stbi_load_from_memory(data.data(), static_cast<int>(data.size()), &width, &height, &channels, 4);
    if (!pixels) {
        throw ResourceError(std::format("can't decode the image: {}", stbi_failure_reason()));
    }
    image->width = static_cast<std::uint32_t>(width);
    image->height = static_cast<std::uint32_t>(height);
    image->rgba.assign(pixels, pixels + static_cast<std::size_t>(width) * height * 4);
    stbi_image_free(pixels);
    (void)max_size;
#else
    (void)data;
    (void)max_size;
    throw ResourceError("this build has no image decoder");
#endif
    return image;
}

} // namespace elg::detail
