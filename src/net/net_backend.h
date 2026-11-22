// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_NET_NET_BACKEND_H
#define ELG_NET_NET_BACKEND_H

// Net's backend interface (ELGSPEC-0001.R4): what downloads need from the operating system,
// implemented in platform/<os>/net.cpp, and the system's proxy settings, in
// platform/<os>/proxy.cpp.

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace elg::detail::net {

// The root certificates the system trusts, for https.
struct SystemRootCertificates {
    // The system's own store, each certificate DER-encoded (Windows, macOS).
    std::vector<std::vector<unsigned char>> certificates;
    // Or PEM bundles to look for, the first one found trusted, besides what OpenSSL's
    // SSL_CERT_FILE and SSL_CERT_DIR name (Linux distributions, which each keep one bundle).
    std::vector<std::filesystem::path> bundles;
};
SystemRootCertificates system_root_certificates();

// The image cache's folder for the app named `app`, in the user's cache folder as the system
// names them, from the environment variables that `get` reads. nullopt when there's none.
std::optional<std::filesystem::path> image_cache_folder(
    std::string_view app, const std::function<std::optional<std::string>(const char*)>& get);

// --- Proxies (net/proxy.h) ---

// An HTTP proxy the system names for a URL.
struct SystemProxyServer {
    std::string host;
    std::string port;
};
// Proxies as Windows lists them (WinHTTP), which find_proxy() picks from for the request's scheme
// and host: `proxies` is "host:port" or "http=host:port;https=host:port", `bypass` is ";"
// separated patterns with * wildcards, "<local>" meaning names without a dot.
struct SystemProxyList {
    std::string proxies;
    std::string bypass;
};
// Why the system's proxy can't be used.
struct SystemProxyError {
    std::string message;
};
// What the system's settings say for a URL: connect directly (std::monostate), through a proxy,
// through one of a list, or not at all.
using SystemProxy = std::variant<std::monostate, SystemProxyServer, SystemProxyList, SystemProxyError>;

// The system's proxy settings for a request to `scheme` ("http" or "https") `host` and `port`.
// Blocks (PAC scripts, WPAD).
SystemProxy system_proxy(std::string_view scheme, std::string_view host, std::string_view port);

// --- Where there is no networking ---

// Throws elg::PlatformError, saying that the system doesn't offer networking. Defined only for
// the system whose build has net/fetch_unavailable.cpp in place of the downloads (Mikros).
[[noreturn]] void networking_unavailable();

} // namespace elg::detail::net

#endif // ELG_NET_NET_BACKEND_H
