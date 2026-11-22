// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Downloads in a build without networking. fetch.cpp, http_cache.cpp and proxy.cpp (Boost,
// OpenSSL) aren't built there; this defines what the rest of the library calls from them, so that
// starting a download fails with the backend's error (net/net_backend.h) and nothing is cached.

#include "net/fetch.h"
#include "net/http_cache.h"
#include "net/net_backend.h"

#include <utility>

namespace elg::detail::net {

// NOLINTNEXTLINE(performance-unnecessary-value-param): the declaration's parameters, unused here.
FetchTask fetch(std::string, std::function<void(FetchResult)>, FetchOptions) {
    networking_unavailable();
}

// A FetchTask that fetch() returned: none is ever made, so there's nothing to stop.
void FetchTask::cancel() const {}

HttpCache::HttpCache(std::filesystem::path directory, std::uint64_t limit)
    : directory_(std::move(directory)), limit_(limit) {}

void HttpCache::clear() {}

std::optional<std::filesystem::path> HttpCache::default_directory() {
    return std::nullopt;
}

} // namespace elg::detail::net
