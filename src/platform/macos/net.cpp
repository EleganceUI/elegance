// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Net's backend (net/net_backend.h) on macOS.

#include "net/net_backend.h"

namespace elg::detail::net {

std::optional<std::filesystem::path> image_cache_folder(
    std::string_view app, const std::function<std::optional<std::string>(const char*)>& get) {
    if (const auto home = get("HOME")) {
        return std::filesystem::path(*home) / "Library" / "Caches" / "Elegance" / app / "Images";
    }
    return std::nullopt;
}

} // namespace elg::detail::net
