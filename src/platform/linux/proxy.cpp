// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// The system's proxy settings on Linux (net/proxy.h): there's no one system setting (desktops
// export theirs as the environment variables find_proxy reads first), so none.

#include "net/proxy.h"

namespace elg::detail::net {

ProxyChoice system_proxy(std::string_view, std::string_view, std::string_view) {
    return std::nullopt; // the environment's settings only
}

} // namespace elg::detail::net
