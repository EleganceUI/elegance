// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_NET_PROXY_H
#define ELG_NET_PROXY_H

// Which proxy a download goes through (design.md §13.3): HTTPS_PROXY / HTTP_PROXY / NO_PROXY when
// set, else the system's settings (macOS: CFNetwork, PAC scripts included; Windows: the user's
// WinHTTP/Internet settings, PAC and WPAD included). Only HTTP proxies: plain requests go to them
// in absolute form, https ones through a CONNECT tunnel. Basic credentials come from the proxy
// URL. Loopback hosts never go through a proxy.

#include <expected>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace elg::detail::net {

struct Proxy {
    std::string host;
    std::string port;
    std::string authorization; // "Basic ..." for Proxy-Authorization, or empty
};

// No proxy (nullopt), a proxy, or why the configured one can't be used.
using ProxyChoice = std::expected<std::optional<Proxy>, std::string>;

// For a request to `scheme` ("http" or "https") `host` and `port`. Blocks (PAC scripts, WPAD):
// call off the network thread. System answers are remembered for a few minutes per host.
ProxyChoice find_proxy(std::string_view scheme, std::string_view host, std::string_view port);

// --- The parts, for tests ---

// A proxy URL: "http://user:password@host:port", or "host:port" (http; port 1080 if missing).
ProxyChoice parse_proxy_url(std::string_view text);
// NO_PROXY: comma or space separated host names (a name matches itself and its subdomains, a
// leading dot is ignored), optionally with ":port", or "*" for every host.
bool bypasses_proxy(std::string_view no_proxy, std::string_view host, std::string_view port);
// From the environment (`get` reads a variable): https_proxy / HTTPS_PROXY for https,
// http_proxy for http (not HTTP_PROXY, which CGI servers let clients set), minus no_proxy /
// NO_PROXY. nullopt: nothing set for this scheme, so the system's settings decide.
std::optional<ProxyChoice> proxy_from_environment(std::string_view scheme, std::string_view host,
                                                  std::string_view port,
                                                  const std::function<std::optional<std::string>(const char*)>& get);
// Windows' manual settings: `proxies` is "host:port" or "http=host:port;https=host:port",
// `bypass` is ";" separated patterns with * wildcards, "<local>" meaning names without a dot.
ProxyChoice proxy_from_windows_settings(std::string_view proxies, std::string_view bypass, std::string_view scheme,
                                        std::string_view host);
// Whether `host` is localhost or a loopback address.
bool is_loopback(std::string_view host);

// The system's settings for a URL (platform/<os>/proxy.cpp): a proxy, nullopt to connect
// directly, or an error.
ProxyChoice system_proxy(std::string_view scheme, std::string_view host, std::string_view port);

} // namespace elg::detail::net

#endif // ELG_NET_PROXY_H
