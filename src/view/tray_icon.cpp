// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// TrayIcon (elg/desktop.h): its picture is decoded here, as images are (media/image_decode.h);
// the tray itself is the platform's (platform/desktop.h, platform/sdl/desktop_sdl.cpp).

#include <elg/desktop.h>

#include "core/log.h"
#include "media/image_decode.h"
#include "platform/desktop.h"

#include <algorithm>
#include <cstdint>
#include <exception>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace elg {

namespace {

// The icon's picture: at most 256 pixels a side (a tray icon's), and within the source's pixel
// limit. nullopt for none, and for one that can't be decoded (with a warning).
std::optional<detail::platform::TrayIconPicture> tray_picture(const ImageSource& icon) {
    try {
        std::vector<std::uint8_t> bytes;
        if (!icon.file.empty()) {
            std::ifstream in(icon.file, std::ios::binary);
            bytes.assign(std::istreambuf_iterator<char>(in), {});
        } else if (!icon.data.empty()) {
            bytes.assign(icon.data.begin(), icon.data.end());
        }
        if (!bytes.empty()) {
            const detail::ImageLimits asked = detail::image_limits(icon.max_pixels, icon.max_side);
            const std::shared_ptr<const detail::DecodedImage> image =
                detail::decode_image(bytes, {.max_pixels = asked.max_pixels, .max_side = std::min(asked.max_side, 256u)});
            // The picture keeps the decoded image, whose pixels it views.
            return detail::platform::TrayIconPicture{image->width, image->height,
                                                     std::shared_ptr<const std::vector<std::uint8_t>>(image, &image->rgba)};
        }
    } catch (const std::exception& failure) {
        detail::log_warning("TrayIcon: {}", failure.what());
    }
    return std::nullopt;
}

} // namespace

TrayIcon::TrayIcon(TrayIconOptions options) {
    detail::platform::require_app("TrayIcon");
    std::optional<detail::platform::TrayIconPicture> picture = tray_picture(options.icon);
    state_ = detail::platform::create_tray_icon(std::move(options), std::move(picture));
}

} // namespace elg
