// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// The system's proxy settings on Windows (net/net_backend.h): WinHTTP, which also finds (WPAD) and
// runs PAC scripts.

#include "net/net_backend.h"

#include "core/log.h"

#include <windows.h>
#include <winhttp.h>

#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

namespace elg::detail::net {

namespace {

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

} // namespace

SystemProxy system_proxy(std::string_view scheme, std::string_view host, std::string_view port) {
    WINHTTP_CURRENT_USER_IE_PROXY_CONFIG settings{};
    if (!WinHttpGetIEProxyConfigForCurrentUser(&settings)) {
        return std::monostate{};
    }
    const std::string manual = narrow(settings.lpszProxy);
    const std::string bypass = narrow(settings.lpszProxyBypass);
    const bool automatic = settings.fAutoDetect || settings.lpszAutoConfigUrl;
    std::optional<SystemProxy> answer;
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
                    answer = SystemProxyList{narrow(info.lpszProxy), narrow(info.lpszProxyBypass)};
                } else {
                    answer = std::monostate{};
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
    return manual.empty() ? SystemProxy(std::monostate{}) : SystemProxy(SystemProxyList{manual, bypass});
}

} // namespace elg::detail::net
