// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Net's backend (net/net_backend.h) on Windows.

#include "net/net_backend.h"

#include <windows.h>
#include <wincrypt.h>

namespace elg::detail::net {

SystemRootCertificates system_root_certificates() {
    SystemRootCertificates roots;
    if (HCERTSTORE system = CertOpenSystemStoreW(0, L"ROOT")) {
        for (PCCERT_CONTEXT certificate = nullptr; (certificate = CertEnumCertificatesInStore(system, certificate));) {
            roots.certificates.emplace_back(certificate->pbCertEncoded,
                                            certificate->pbCertEncoded + certificate->cbCertEncoded);
        }
        CertCloseStore(system, 0);
    }
    return roots;
}

std::optional<std::filesystem::path> image_cache_folder(
    std::string_view app, const std::function<std::optional<std::string>(const char*)>& get) {
    if (const auto base = get("LOCALAPPDATA")) {
        return std::filesystem::path(*base) / "Elegance" / app / "Images";
    }
    return std::nullopt;
}

} // namespace elg::detail::net
