// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Images on macOS (media/image_decode.h): ImageIO.

#include "media/image_decode.h"

#include <elg/error.h>

#include <algorithm>
#include <utility>

#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>

namespace elg::detail {

namespace {

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

} // namespace

StoredImageInfo platform_probe_image(std::span<const std::uint8_t> data) {
    const SourceInfo info = source_info(image_source(data).get());
    return {info.width, info.height, info.orientation};
}

int platform_decode_image(std::span<const std::uint8_t> data, std::uint32_t max_size, DecodedImage& image) {
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
    image.width = static_cast<std::uint32_t>(width);
    image.height = static_cast<std::uint32_t>(height);
    image.rgba.resize(width * height * 4);
    // Drawing converts from the image's color profile to sRGB. Bitmap contexts are always
    // premultiplied, so the alpha is undone afterwards.
    const CFRef<CGColorSpaceRef> srgb(CGColorSpaceCreateWithName(kCGColorSpaceSRGB));
    const CFRef<CGContextRef> context(CGBitmapContextCreate(image.rgba.data(), width, height, 8, width * 4, srgb.get(),
                                                            static_cast<std::uint32_t>(kCGImageAlphaPremultipliedLast) |
                                                                static_cast<std::uint32_t>(kCGBitmapByteOrder32Big)));
    if (!context) {
        throw ResourceError("can't decode the image");
    }
    CGContextSetBlendMode(context.get(), kCGBlendModeCopy);
    CGContextDrawImage(context.get(), CGRectMake(0, 0, static_cast<CGFloat>(width), static_cast<CGFloat>(height)),
                       picture.get());
    for (std::size_t i = 0; i < image.rgba.size(); i += 4) {
        const unsigned alpha = image.rgba[i + 3];
        if (alpha != 0 && alpha != 255) {
            for (std::size_t c = 0; c < 3; ++c) {
                image.rgba[i + c] = static_cast<std::uint8_t>(std::min(255u, (image.rgba[i + c] * 255u + alpha / 2) / alpha));
            }
        }
    }
    return info.orientation;
}

std::optional<std::vector<unsigned char>> encode_png(std::span<const std::uint8_t> bgra, std::uint32_t width,
                                                     std::uint32_t height) {
    const CFRef<CFDataRef> bytes(CFDataCreate(nullptr, bgra.data(), static_cast<CFIndex>(bgra.size())));
    const CFRef<CGDataProviderRef> provider(bytes ? CGDataProviderCreateWithCFData(bytes.get()) : nullptr);
    const CFRef<CGColorSpaceRef> srgb(CGColorSpaceCreateWithName(kCGColorSpaceSRGB));
    if (!provider || !srgb) {
        return std::nullopt;
    }
    // BGRA in memory is ARGB read as little-endian 32-bit words.
    const CFRef<CGImageRef> picture(CGImageCreate(width, height, 8, 32, static_cast<std::size_t>(width) * 4,
                                                  srgb.get(),
                                                  static_cast<std::uint32_t>(kCGImageAlphaNoneSkipFirst) |
                                                      static_cast<std::uint32_t>(kCGBitmapByteOrder32Little),
                                                  provider.get(), nullptr, false, kCGRenderingIntentDefault));
    const CFRef<CFMutableDataRef> png(CFDataCreateMutable(nullptr, 0));
    if (!picture || !png) {
        return std::nullopt;
    }
    const CFRef<CGImageDestinationRef> destination(CGImageDestinationCreateWithData(png.get(), CFSTR("public.png"), 1, nullptr));
    if (!destination) {
        return std::nullopt;
    }
    CGImageDestinationAddImage(destination.get(), picture.get(), nullptr);
    if (!CGImageDestinationFinalize(destination.get())) {
        return std::nullopt;
    }
    const auto* begin = CFDataGetBytePtr(png.get());
    return std::vector<unsigned char>(begin, begin + CFDataGetLength(png.get()));
}

} // namespace elg::detail
