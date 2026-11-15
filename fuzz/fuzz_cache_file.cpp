// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// The HTTP cache's files: the input as a file's contents, read as HttpCache::find reads them.
// Whatever it holds, the result is an entry for the URL asked for, or none.

#include "net/http_cache.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <sstream>
#include <string>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size);

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    namespace net = elg::detail::net;
    const std::string url = "https://example.com/a.png";
    std::istringstream in(std::string(reinterpret_cast<const char*>(data), size));
    const std::optional<net::CacheEntry> entry = net::read_cache_entry(in, url);
    if (entry) {
        if (entry->url != url || (entry->redirect() && entry->location.empty())) {
            std::abort();
        }
        static_cast<void>(entry->fresh(net::unix_now()));
        static_cast<void>(entry->usable_stale());
    }
    return 0;
}
