// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// The system's proxy settings on macOS (net/proxy.h): CFNetwork, which also runs PAC scripts.

#include "net/proxy.h"

#include "core/log.h"

#include <CFNetwork/CFNetwork.h>
#include <CoreFoundation/CoreFoundation.h>

#include <chrono>
#include <format>
#include <string>

namespace elg::detail::net {

namespace {

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

} // namespace

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

} // namespace elg::detail::net
