// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Net's backend (net/net_backend.h) on Mikros, which has no networking: what the stand-in for
// the downloads (net/fetch_unavailable.cpp) fails with.

#include "net/net_backend.h"

#include "platform/mikros/unavailable.h"

namespace elg::detail::net {

void networking_unavailable() {
    platform::mikros::unavailable("networking");
}

} // namespace elg::detail::net
