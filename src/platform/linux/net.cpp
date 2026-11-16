// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Net's backend (net/net_backend.h) on Linux and other Unix desktops.

#include "net/net_backend.h"

namespace elg::detail::net {

std::optional<std::filesystem::path> image_cache_folder(
    std::string_view app, const std::function<std::optional<std::string>(const char*)>& get) {
    if (const auto xdg = get("XDG_CACHE_HOME"); xdg && !xdg->empty()) {
        return std::filesystem::path(*xdg) / "elegance" / app / "images";
    }
    if (const auto home = get("HOME")) {
        return std::filesystem::path(*home) / ".cache" / "elegance" / app / "images";
    }
    return std::nullopt;
}

} // namespace elg::detail::net
