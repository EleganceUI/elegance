// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Proxy settings: the environment's, then the system's, read through the system's own APIs in
// platform/<os>/proxy.cpp (PAC scripts run in CFNetwork or WinHTTP, never here).

#include "net/proxy.h"

#include "core/log.h"
#include "platform/generic/platform.h"

#include <boost/asio/ip/address.hpp>
#include <boost/url.hpp>
#include <openssl/evp.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <format>
#include <map>
#include <mutex>
#include <vector>

namespace elg::detail::net {

namespace {

std::string lower(std::string_view text) {
    std::string result(text);
    std::ranges::transform(result, result.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}

std::string_view trim(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) {
        text.remove_suffix(1);
    }
    return text;
}

// Splits on any of `separators`, dropping empty parts.
std::vector<std::string_view> split(std::string_view text, std::string_view separators) {
    std::vector<std::string_view> parts;
    while (!text.empty()) {
        const std::size_t end = text.find_first_of(separators);
        if (const std::string_view part = trim(text.substr(0, end)); !part.empty()) {
            parts.push_back(part);
        }
        if (end == std::string_view::npos) {
            break;
        }
        text.remove_prefix(end + 1);
    }
    return parts;
}

std::string base64(std::string_view data) {
    std::string out(4 * ((data.size() + 2) / 3), '\0');
    const int written = EVP_EncodeBlock(reinterpret_cast<unsigned char*>(out.data()),
                                        reinterpret_cast<const unsigned char*>(data.data()), static_cast<int>(data.size()));
    out.resize(static_cast<std::size_t>(written));
    return out;
}

// `pattern` with * wildcards against `text`, both lowercase.
bool wildcard_match(std::string_view pattern, std::string_view text) {
    if (pattern.empty()) {
        return text.empty();
    }
    if (pattern.front() == '*') {
        for (std::size_t skip = 0; skip <= text.size(); ++skip) {
            if (wildcard_match(pattern.substr(1), text.substr(skip))) {
                return true;
            }
        }
        return false;
    }
    return !text.empty() && pattern.front() == text.front() && wildcard_match(pattern.substr(1), text.substr(1));
}

std::string strip_brackets(std::string_view host) {
    if (host.size() >= 2 && host.front() == '[' && host.back() == ']') {
        host = host.substr(1, host.size() - 2);
    }
    return lower(host);
}

} // namespace

ProxyChoice parse_proxy_url(std::string_view text) {
    text = trim(text);
    std::string full(text);
    if (full.find("://") == std::string::npos) {
        full = "http://" + full;
    }
    const auto parsed = boost::urls::parse_uri(full);
    if (!parsed || parsed->host_address().empty()) {
        return std::unexpected(std::format("the proxy setting isn't a URL: {}", text));
    }
    if (parsed->scheme_id() != boost::urls::scheme::http) {
        return std::unexpected(std::format("the proxy isn't supported (only http:// proxies are): {}", parsed->scheme()));
    }
    Proxy proxy;
    proxy.host = parsed->host_address();
    proxy.port = parsed->has_port() && !parsed->port().empty() ? std::string(parsed->port()) : "1080";
    if (parsed->has_userinfo()) {
        proxy.authorization = "Basic " + base64(std::format("{}:{}", parsed->user(), parsed->password()));
    }
    return proxy;
}

bool is_loopback(std::string_view host) {
    const std::string name = strip_brackets(host);
    if (name == "localhost" || name.ends_with(".localhost") || name == "::1") {
        return true;
    }
    boost::system::error_code error;
    const auto address = boost::asio::ip::make_address(name, error);
    return !error && address.is_loopback();
}

bool bypasses_proxy(std::string_view no_proxy, std::string_view host, std::string_view port) {
    const std::string name = strip_brackets(host);
    for (std::string_view entry : split(no_proxy, ", ")) {
        if (entry == "*") {
            return true;
        }
        std::string pattern = lower(entry);
        std::string pattern_port;
        // "host:port", "[v6]:port"; a bare v6 address has more than one colon.
        if (const std::size_t colon = pattern.rfind(':');
            colon != std::string::npos && pattern.find(':') == colon && pattern.front() != '[') {
            pattern_port = pattern.substr(colon + 1);
            pattern.resize(colon);
        } else if (pattern.front() == '[') {
            if (const std::size_t close = pattern.find(']'); close != std::string::npos) {
                if (close + 1 < pattern.size() && pattern[close + 1] == ':') {
                    pattern_port = pattern.substr(close + 2);
                }
                pattern = pattern.substr(1, close - 1);
            }
        }
        if (!pattern_port.empty() && pattern_port != port) {
            continue;
        }
        if (pattern.starts_with("*.")) {
            pattern.erase(0, 1);
        }
        if (pattern.starts_with('.')) {
            pattern.erase(0, 1);
        }
        if (pattern.empty()) {
            continue;
        }
        if (name == pattern || (name.size() > pattern.size() && name.ends_with(pattern) &&
                                name[name.size() - pattern.size() - 1] == '.')) {
            return true;
        }
    }
    return false;
}

std::optional<ProxyChoice> proxy_from_environment(std::string_view scheme, std::string_view host,
                                                  std::string_view port,
                                                  const std::function<std::optional<std::string>(const char*)>& get) {
    std::optional<std::string> setting;
    if (scheme == "https") {
        setting = get("https_proxy");
        if (!setting || setting->empty()) {
            setting = get("HTTPS_PROXY");
        }
    } else {
        setting = get("http_proxy");
    }
    if (!setting || trim(*setting).empty()) {
        return std::nullopt;
    }
    std::optional<std::string> no_proxy = get("no_proxy");
    if (!no_proxy || no_proxy->empty()) {
        no_proxy = get("NO_PROXY");
    }
    if (no_proxy && bypasses_proxy(*no_proxy, host, port)) {
        return ProxyChoice(std::nullopt);
    }
    return parse_proxy_url(*setting);
}

ProxyChoice proxy_from_windows_settings(std::string_view proxies, std::string_view bypass, std::string_view scheme,
                                        std::string_view host) {
    const std::string name = strip_brackets(host);
    for (std::string_view entry : split(bypass, "; ")) {
        std::string pattern = lower(entry);
        if (pattern == "<local>") {
            if (name.find('.') == std::string::npos && name.find(':') == std::string::npos) {
                return std::nullopt;
            }
            continue;
        }
        if (const std::size_t scheme_end = pattern.find("://"); scheme_end != std::string::npos) {
            pattern.erase(0, scheme_end + 3);
        }
        if (const std::size_t colon = pattern.rfind(':');
            colon != std::string::npos && pattern.find(':') == colon) { // "host:port"; the port isn't compared
            pattern.resize(colon);
        }
        if (wildcard_match(pattern, name)) {
            return std::nullopt;
        }
    }
    std::optional<std::string_view> any;
    for (std::string_view entry : split(proxies, "; ")) {
        if (const std::size_t equals = entry.find('='); equals != std::string_view::npos) {
            if (lower(trim(entry.substr(0, equals))) == scheme) {
                return parse_proxy_url(entry.substr(equals + 1));
            }
        } else if (!any) {
            any = entry;
        }
    }
    return any ? parse_proxy_url(*any) : ProxyChoice(std::nullopt);
}

ProxyChoice find_proxy(std::string_view scheme, std::string_view host, std::string_view port) {
    if (is_loopback(host)) {
        return std::nullopt;
    }
    if (std::optional<ProxyChoice> from_environment =
            proxy_from_environment(scheme, host, port, [](const char* name) { return platform::environment_variable(name); })) {
        return *from_environment;
    }
    // The system's answer, remembered a while (PAC scripts and WPAD are slow).
    static std::mutex mutex;
    static std::map<std::string, std::pair<ProxyChoice, std::chrono::steady_clock::time_point>> remembered;
    const std::string key = std::format("{}://{}:{}", scheme, lower(host), port);
    const auto now = std::chrono::steady_clock::now();
    {
        const std::lock_guard lock(mutex);
        if (const auto it = remembered.find(key); it != remembered.end() && it->second.second > now) {
            return it->second.first;
        }
    }
    ProxyChoice choice = system_proxy(scheme, host, port);
    const std::lock_guard lock(mutex);
    if (remembered.size() > 256) {
        remembered.clear();
    }
    remembered.insert_or_assign(key, std::pair(choice, now + std::chrono::minutes(5)));
    return choice;
}

} // namespace elg::detail::net
