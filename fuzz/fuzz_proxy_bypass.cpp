// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Proxy settings: bypass lists (NO_PROXY and Windows' patterns with * wildcards) matched against a
// host, and proxy URLs. The input's lines are the settings, the host and the port.

#include "net/proxy.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size);

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    namespace net = elg::detail::net;
    // A bypass list or pattern, a host, a port, and a proxy setting.
    std::array<std::string, 4> parts;
    std::size_t part = 0;
    for (std::size_t i = 0; i < size && part < parts.size(); ++i) {
        if (data[i] == '\n') {
            ++part;
        } else {
            parts[part] += static_cast<char>(data[i]);
        }
    }
    const auto& [bypass, host, port, setting] = parts;
    static_cast<void>(net::wildcard_match(bypass, host));
    static_cast<void>(net::bypasses_proxy(bypass, host, port));
    static_cast<void>(net::proxy_from_windows_settings(setting, bypass, "https", host));
    static_cast<void>(net::parse_proxy_url(setting));
    static_cast<void>(net::is_loopback(host));
    static_cast<void>(net::proxy_from_environment("https", host, port, [&](const char* name) -> std::optional<std::string> {
        return std::string_view(name) == "no_proxy" ? bypass : setting;
    }));
    return 0;
}
