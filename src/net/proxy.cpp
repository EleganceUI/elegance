// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Proxy settings: the environment's, then the system's, read through the system's own APIs (PAC
// scripts run in CFNetwork or WinHTTP, never here).

#include "net/proxy.h"

#include "core/log.h"
#include "platform/platform.h"

#include <boost/asio/ip/address.hpp>
#include <boost/url.hpp>
#include <openssl/evp.h>

#if defined(__APPLE__)
#include <CFNetwork/CFNetwork.h>
#include <CoreFoundation/CoreFoundation.h>
#elif defined(_WIN32)
#include <windows.h>
#include <winhttp.h>
#endif

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

#if defined(__APPLE__)

std::string cf_string(CFStringRef value) {
    if (!value) {
        return {};
    }
    if (const char* fast = CFStringGetCStringPtr(value, kCFStringEncodingUTF8)) {
        return fast;
    }
    const CFIndex size = CFStringGetMaximumSizeForEncoding(CFStringGetLength(value), kCFStringEncodingUTF8) + 1;
    std::string out(static_cast<std::size_t>(size), '\0');
    if (!CFStringGetCString(value, out.data(), size, kCFStringEncodingUTF8)) {
        return {};
    }
    out.resize(std::char_traits<char>::length(out.c_str()));
    return out;
}

struct PacOutcome {
    CFArrayRef proxies = nullptr;
    bool done = false;
};

void pac_finished(void* client, CFArrayRef proxies, CFErrorRef) {
    auto* outcome = static_cast<PacOutcome*>(client);
    if (proxies) {
        outcome->proxies = static_cast<CFArrayRef>(CFRetain(proxies));
    }
    outcome->done = true;
    CFRunLoopStop(CFRunLoopGetCurrent());
}

// Runs a PAC script (by URL, or the script itself) for `target` on this thread's run loop.
CFArrayRef run_pac(CFDictionaryRef entry, CFURLRef target) {
    PacOutcome outcome;
    CFStreamClientContext context{0, &outcome, nullptr, nullptr, nullptr};
    CFRunLoopSourceRef source = nullptr;
    const auto type = static_cast<CFStringRef>(CFDictionaryGetValue(entry, kCFProxyTypeKey));
    if (CFEqual(type, kCFProxyTypeAutoConfigurationURL)) {
        const auto script_url = static_cast<CFURLRef>(CFDictionaryGetValue(entry, kCFProxyAutoConfigurationURLKey));
        if (script_url) {
            source = CFNetworkExecuteProxyAutoConfigurationURL(script_url, target, pac_finished, &context);
        }
    } else {
        const auto script = static_cast<CFStringRef>(CFDictionaryGetValue(entry, kCFProxyAutoConfigurationJavaScriptKey));
        if (script) {
            source = CFNetworkExecuteProxyAutoConfigurationScript(script, target, pac_finished, &context);
        }
    }
    if (!source) {
        return nullptr;
    }
    const CFStringRef mode = CFSTR("elg.proxy");
    CFRunLoopAddSource(CFRunLoopGetCurrent(), source, mode);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!outcome.done && std::chrono::steady_clock::now() < deadline) {
        CFRunLoopRunInMode(mode, 1, false);
    }
    CFRunLoopRemoveSource(CFRunLoopGetCurrent(), source, mode);
    CFRunLoopSourceInvalidate(source);
    CFRelease(source);
    return outcome.proxies;
}

// The first usable entry of CFNetwork's list for a URL.
ProxyChoice choose(CFArrayRef proxies, CFURLRef target, int depth = 0) {
    bool unsupported = false;
    for (CFIndex i = 0; proxies && i < CFArrayGetCount(proxies); ++i) {
        const auto entry = static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(proxies, i));
        const auto type = static_cast<CFStringRef>(CFDictionaryGetValue(entry, kCFProxyTypeKey));
        if (!type) {
            continue;
        }
        if (CFEqual(type, kCFProxyTypeNone)) {
            return std::nullopt;
        }
        if (CFEqual(type, kCFProxyTypeHTTP) || CFEqual(type, kCFProxyTypeHTTPS)) {
            const auto host = static_cast<CFStringRef>(CFDictionaryGetValue(entry, kCFProxyHostNameKey));
            const auto port = static_cast<CFNumberRef>(CFDictionaryGetValue(entry, kCFProxyPortNumberKey));
            int number = 0;
            if (host && port && CFNumberGetValue(port, kCFNumberIntType, &number) && number > 0) {
                return Proxy{cf_string(host), std::to_string(number), {}};
            }
            continue;
        }
        if ((CFEqual(type, kCFProxyTypeAutoConfigurationURL) || CFEqual(type, kCFProxyTypeAutoConfigurationJavaScript)) &&
            depth == 0) {
            CFArrayRef resolved = run_pac(entry, target);
            if (!resolved) {
                log_warning("net: the proxy auto-configuration script failed; connecting directly");
                return std::nullopt;
            }
            ProxyChoice choice = choose(resolved, target, depth + 1);
            CFRelease(resolved);
            return choice;
        }
        unsupported = true; // SOCKS, FTP
    }
    if (unsupported) {
        return std::unexpected("the system's proxy isn't supported (only HTTP proxies are)");
    }
    return std::nullopt;
}

ProxyChoice system_proxy(std::string_view scheme, std::string_view host, std::string_view port) {
    const std::string text = std::format("{}://{}:{}/", scheme, host.find(':') != std::string_view::npos
                                                                     ? std::format("[{}]", host)
                                                                     : std::string(host), port);
    CFURLRef target = CFURLCreateWithBytes(nullptr, reinterpret_cast<const UInt8*>(text.data()),
                                           static_cast<CFIndex>(text.size()), kCFStringEncodingUTF8, nullptr);
    CFDictionaryRef settings = CFNetworkCopySystemProxySettings();
    if (!target || !settings) {
        if (target) {
            CFRelease(target);
        }
        if (settings) {
            CFRelease(settings);
        }
        return std::nullopt;
    }
    CFArrayRef proxies = CFNetworkCopyProxiesForURL(target, settings);
    ProxyChoice choice = choose(proxies, target);
    if (proxies) {
        CFRelease(proxies);
    }
    CFRelease(settings);
    CFRelease(target);
    return choice;
}

#elif defined(_WIN32)

std::wstring widen(std::string_view text) {
    if (text.empty()) {
        return {};
    }
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size);
    return out;
}

std::string narrow(const wchar_t* text) {
    if (!text || !*text) {
        return {};
    }
    const int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), size, nullptr, nullptr);
    out.resize(out.size() - 1); // the terminator
    return out;
}

ProxyChoice system_proxy(std::string_view scheme, std::string_view host, std::string_view port) {
    WINHTTP_CURRENT_USER_IE_PROXY_CONFIG settings{};
    if (!WinHttpGetIEProxyConfigForCurrentUser(&settings)) {
        return std::nullopt;
    }
    const std::string manual = narrow(settings.lpszProxy);
    const std::string bypass = narrow(settings.lpszProxyBypass);
    const bool automatic = settings.fAutoDetect || settings.lpszAutoConfigUrl;
    std::optional<ProxyChoice> answer;
    if (automatic) {
        if (HINTERNET session = WinHttpOpen(L"Elegance", WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME,
                                            WINHTTP_NO_PROXY_BYPASS, 0)) {
            WINHTTP_AUTOPROXY_OPTIONS options{};
            if (settings.lpszAutoConfigUrl) {
                options.dwFlags = WINHTTP_AUTOPROXY_CONFIG_URL;
                options.lpszAutoConfigUrl = settings.lpszAutoConfigUrl;
            } else {
                options.dwFlags = WINHTTP_AUTOPROXY_AUTO_DETECT;
                options.dwAutoDetectFlags = WINHTTP_AUTO_DETECT_TYPE_DHCP | WINHTTP_AUTO_DETECT_TYPE_DNS_A;
            }
            options.fAutoLogonIfChallenged = TRUE;
            const std::wstring url = widen(std::format("{}://{}:{}/", scheme, host.find(':') != std::string_view::npos
                                                                                  ? std::format("[{}]", host)
                                                                                  : std::string(host), port));
            WINHTTP_PROXY_INFO info{};
            if (WinHttpGetProxyForUrl(session, url.c_str(), &options, &info)) {
                if (info.dwAccessType == WINHTTP_ACCESS_TYPE_NAMED_PROXY && info.lpszProxy) {
                    answer = proxy_from_windows_settings(narrow(info.lpszProxy), narrow(info.lpszProxyBypass), scheme, host);
                } else {
                    answer = ProxyChoice(std::nullopt);
                }
                if (info.lpszProxy) {
                    GlobalFree(info.lpszProxy);
                }
                if (info.lpszProxyBypass) {
                    GlobalFree(info.lpszProxyBypass);
                }
            }
            WinHttpCloseHandle(session);
        }
    }
    for (LPWSTR text : {settings.lpszAutoConfigUrl, settings.lpszProxy, settings.lpszProxyBypass}) {
        if (text) {
            GlobalFree(text);
        }
    }
    if (answer) {
        return *answer;
    }
    // No script, or it failed: the manual settings.
    return manual.empty() ? ProxyChoice(std::nullopt) : proxy_from_windows_settings(manual, bypass, scheme, host);
}

#else

ProxyChoice system_proxy(std::string_view, std::string_view, std::string_view) {
    return std::nullopt; // the environment's settings only
}

#endif

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
