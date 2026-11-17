// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Net's backend (net/net_backend.h) on macOS.

#include "net/net_backend.h"

#include <Security/Security.h>

namespace elg::detail::net {

SystemRootCertificates system_root_certificates() {
    SystemRootCertificates roots;
    CFArrayRef anchors = nullptr;
    if (SecTrustCopyAnchorCertificates(&anchors) == errSecSuccess && anchors) {
        for (CFIndex i = 0; i < CFArrayGetCount(anchors); ++i) {
            const auto certificate = static_cast<SecCertificateRef>(const_cast<void*>(CFArrayGetValueAtIndex(anchors, i)));
            if (CFDataRef der = SecCertificateCopyData(certificate)) {
                const UInt8* bytes = CFDataGetBytePtr(der);
                roots.certificates.emplace_back(bytes, bytes + CFDataGetLength(der));
                CFRelease(der);
            }
        }
        CFRelease(anchors);
    }
    return roots;
}

std::optional<std::filesystem::path> image_cache_folder(
    std::string_view app, const std::function<std::optional<std::string>(const char*)>& get) {
    if (const auto home = get("HOME")) {
        return std::filesystem::path(*home) / "Library" / "Caches" / "Elegance" / app / "Images";
    }
    return std::nullopt;
}

} // namespace elg::detail::net
