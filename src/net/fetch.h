// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_NET_FETCH_H
#define ELG_NET_FETCH_H

// Downloads for AsyncImage, on Boost.Beast (HTTP/1.1), Boost.Asio and OpenSSL. Internal: Elegance
// has no public networking API, so apps bring their own library for anything beyond images.
// Connections are kept open and reused per host; proxies come from the environment or the system
// (net/proxy.h); responses can be cached on disk (net/http_cache.h).

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace elg::detail::net {

class HttpCache;

struct FetchOptions {
    std::chrono::milliseconds timeout{30'000}; // the whole request, redirects included
    std::size_t max_body = 64ull << 20;
    int max_redirects = 5;
    // PEM certificates to trust instead of the system's (tests).
    std::string root_certificates;
    // A proxy URL to use instead of the environment's and the system's; "" for none (tests).
    std::optional<std::string> proxy;
    // Answers from this cache while fresh, revalidates with it, and stores in it (nullptr: none).
    std::shared_ptr<HttpCache> cache;
};

// The body of a 2xx response, or why there isn't one, in words ("HTTP 404", "the request timed
// out", "the secure connection failed: ...", "cancelled").
using FetchResult = std::expected<std::vector<std::uint8_t>, std::string>;

// A download in flight. Copies refer to the same one.
class FetchTask {
public:
    FetchTask() = default;
    // Stops it; its callback won't run. Any thread.
    void cancel() const;

    struct State;
    explicit FetchTask(std::shared_ptr<State> state) noexcept : state_(std::move(state)) {}

private:
    std::shared_ptr<State> state_;
};

// GETs an http(s) URL, following redirects (never from https to http). TLS checks the
// certificate against the system's roots and the host name. With a cache, a stale copy answers
// when the server can't be reached (unless it must be revalidated). `done` runs once, on the
// network thread, unless cancelled first. Any thread.
FetchTask fetch(std::string url, std::function<void(FetchResult)> done, FetchOptions options = {});

} // namespace elg::detail::net

#endif // ELG_NET_FETCH_H
