// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_NET_NET_BACKEND_H
#define ELG_NET_NET_BACKEND_H

// Net's backend interface (ELGSPEC-0001.R4): what downloads need from the operating system,
// implemented in platform/<os>/net.cpp.

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace elg::detail::net {

// The image cache's folder for the app named `app`, in the user's cache folder as the system
// names them, from the environment variables that `get` reads. nullopt when there's none.
std::optional<std::filesystem::path> image_cache_folder(
    std::string_view app, const std::function<std::optional<std::string>(const char*)>& get);

} // namespace elg::detail::net

#endif // ELG_NET_NET_BACKEND_H
