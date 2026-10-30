// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Images on Linux (media/image_decode.h): stb_image and stb_image_write.

#include "media/image_decode.h"

#include <elg/error.h>

#include <format>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#include <stb_image.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include <stb_image_write.h>

namespace elg::detail {

StoredImageInfo platform_probe_image(std::span<const std::uint8_t> data) {
    int width = 0;
    int height = 0;
    int channels = 0;
    if (!stbi_info_from_memory(data.data(), static_cast<int>(data.size()), &width, &height, &channels)) {
        throw ResourceError(std::format("can't read the image: {}", stbi_failure_reason()));
    }
    return {static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height), 1};
}

int platform_decode_image(std::span<const std::uint8_t> data, std::uint32_t max_size, DecodedImage& image) {
    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_uc* pixels = stbi_load_from_memory(data.data(), static_cast<int>(data.size()), &width, &height, &channels, 4);
    if (!pixels) {
        throw ResourceError(std::format("can't decode the image: {}", stbi_failure_reason()));
    }
    image.width = static_cast<std::uint32_t>(width);
    image.height = static_cast<std::uint32_t>(height);
    image.rgba.assign(pixels, pixels + static_cast<std::size_t>(width) * height * 4);
    stbi_image_free(pixels);
    (void)max_size; // TODO: scale down like the other platforms
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
