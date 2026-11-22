// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Images on Linux and Mikros (media/image_codec_backend.h): stb_image and stb_image_write, with the EXIF orientation
// of JPEG and PNG files read here, and images scaled down here too (stb_image_resize2 stores
// floats through misaligned 64-bit pointers).

#include "media/image_codec_backend.h"

#include <elg/error.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <climits>
#include <cmath>
#include <cstdarg>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <format>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace {

// stb_image copies 0 bytes from or to a null pointer in places (an empty first IDAT chunk), which
// memcpy and memset don't allow. It gets these, which do nothing for 0 bytes.
void* stb_copy(void* to, const void* from, std::size_t size) noexcept {
    return size == 0 ? to : std::memcpy(to, from, size);
}
void* stb_fill(void* to, int value, std::size_t size) noexcept {
    return size == 0 ? to : std::memset(to, value, size);
}

} // namespace

// Internal linkage: an app that uses stb too gets no duplicate symbols. The standard headers stb
// includes are included above, so the macros only reach stb's own code.
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define memcpy(to, from, size) stb_copy(to, from, size)
#define memset(to, value, size) stb_fill(to, value, size)
#include <stb_image.h>
#undef memcpy
#undef memset
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include <stb_image_write.h>

namespace elg::detail {

namespace {

// stb takes the data's length as an int.
int stb_length(std::span<const std::uint8_t> data) {
    if (data.size() > static_cast<std::size_t>(INT_MAX)) {
        throw ResourceError("the image data is larger than 2 GB");
    }
    return static_cast<int>(data.size());
}

// RGBA pixels (w x h, 4 bytes each) scaled down to out_w x out_h, neither larger than the source:
// each output pixel averages the source pixels in its share of the image, colors weighted by
// alpha so transparent pixels don't tint their neighbors. The shares split the source rows and
// columns between the output's, so each source pixel is read once. Integer sums: an average of
// up to 2^40 pixels can't overflow.
void scale_down(const std::uint8_t* pixels, std::uint32_t w, std::uint32_t h, std::uint32_t out_w, std::uint32_t out_h,
                std::vector<std::uint8_t>& out) {
    const auto share = [](std::uint32_t i, std::uint32_t from, std::uint32_t to) {
        return static_cast<std::uint32_t>(std::uint64_t{i} * from / to); // where output i's share starts
    };
    std::vector<std::uint32_t> columns(out_w + 1);
    for (std::uint32_t x = 0; x <= out_w; ++x) {
        columns[x] = share(x, w, out_w);
    }
    out.assign(std::size_t{out_w} * out_h * 4, 0);
    std::vector<std::uint64_t> sums(std::size_t{out_w} * 4); // alpha-weighted red, green, blue, then alpha
    for (std::uint32_t oy = 0; oy < out_h; ++oy) {
        const std::uint32_t y0 = share(oy, h, out_h);
        const std::uint32_t y1 = std::max(y0 + 1, share(oy + 1, h, out_h));
        std::ranges::fill(sums, 0);
        for (std::uint32_t y = y0; y < y1; ++y) {
            const std::uint8_t* row = pixels + std::size_t{y} * w * 4;
            for (std::uint32_t ox = 0; ox < out_w; ++ox) {
                std::uint64_t* sum = &sums[std::size_t{ox} * 4];
                for (std::uint32_t x = columns[ox]; x < std::max(columns[ox] + 1, columns[ox + 1]); ++x) {
                    const std::uint8_t* p = row + std::size_t{x} * 4;
                    sum[0] += std::uint64_t{p[0]} * p[3];
                    sum[1] += std::uint64_t{p[1]} * p[3];
                    sum[2] += std::uint64_t{p[2]} * p[3];
                    sum[3] += p[3];
                }
            }
        }
        for (std::uint32_t ox = 0; ox < out_w; ++ox) {
            const std::uint64_t* sum = &sums[std::size_t{ox} * 4];
            const std::uint64_t count = std::uint64_t{y1 - y0} * std::max(1u, columns[ox + 1] - columns[ox]);
            std::uint8_t* q = &out[(std::size_t{oy} * out_w + ox) * 4];
            for (int c = 0; c < 3; ++c) {
                q[c] = sum[3] == 0 ? 0 : static_cast<std::uint8_t>((sum[c] + sum[3] / 2) / sum[3]);
            }
            q[3] = static_cast<std::uint8_t>((sum[3] + count / 2) / count);
        }
    }
}


} // namespace

StoredImageInfo platform_probe_image(std::span<const std::uint8_t> data) {
    int width = 0;
    int height = 0;
    int channels = 0;
    if (!stbi_info_from_memory(data.data(), stb_length(data), &width, &height, &channels)) {
        throw ResourceError(std::format("can't read the image: {}", stbi_failure_reason()));
    }
    if (width <= 0 || height <= 0) {
        throw ResourceError("the image has no pixels"); // some formats let stb read a size of 0
    }
    // stb_image reads no orientation: the media layer finds the one the file records.
    return {static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height), 1};
}

int platform_decode_image(std::span<const std::uint8_t> data, std::uint32_t max_size, DecodedImage& image) {
    int width = 0;
    int height = 0;
    int channels = 0;
    const std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels(
        stbi_load_from_memory(data.data(), stb_length(data), &width, &height, &channels, 4), &stbi_image_free);
    if (!pixels) {
        throw ResourceError(std::format("can't decode the image: {}", stbi_failure_reason()));
    }
    if (width <= 0 || height <= 0) {
        throw ResourceError("the image has no pixels");
    }
    const auto w = static_cast<std::uint32_t>(width);
    const auto h = static_cast<std::uint32_t>(height);
    if (max_size > 0 && (w > max_size || h > max_size)) {
        // Scaled down to fit, keeping the aspect ratio.
        const double k = static_cast<double>(max_size) / std::max(w, h);
        const auto out_w = std::clamp(static_cast<std::uint32_t>(std::lround(w * k)), 1u, w);
        const auto out_h = std::clamp(static_cast<std::uint32_t>(std::lround(h * k)), 1u, h);
        scale_down(pixels.get(), w, h, out_w, out_h, image.rgba);
        image.width = out_w;
        image.height = out_h;
    } else {
        image.width = w;
        image.height = h;
        image.rgba.assign(pixels.get(), pixels.get() + std::size_t{w} * h * 4);
    }
    return 1;
}

std::optional<std::vector<unsigned char>> encode_png(std::span<const std::uint8_t> bgra, std::uint32_t width,
                                                     std::uint32_t height) {
    std::vector<std::uint8_t> rgba(bgra.begin(), bgra.end());
    for (std::size_t i = 0; i + 3 < rgba.size(); i += 4) {
        std::swap(rgba[i], rgba[i + 2]);
    }
    std::vector<unsigned char> png;
    const auto append = [](void* context, void* data, int size) {
        auto* out = static_cast<std::vector<unsigned char>*>(context);
        const auto* bytes = static_cast<const unsigned char*>(data);
        out->insert(out->end(), bytes, bytes + size);
    };
    if (!stbi_write_png_to_func(append, &png, static_cast<int>(width), static_cast<int>(height), 4, rgba.data(),
                                static_cast<int>(width) * 4)) {
        return std::nullopt;
    }
    return png;
}

} // namespace elg::detail
