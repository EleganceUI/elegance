// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_HTTP_H
#define ELG_HTTP_H

// HTTP(S) requests (design.md §13.3) on the system's own networking: WinHTTP on Windows (system
// certificates and proxy settings, HTTP/2, gzip). Requests run on background threads; results
// arrive on the UI thread, where state may be written.
//
//   http::fetch({.url = "https://api.example.com/items"}, [=](const http::Result& result) {
//       if (result) { items = parse(result->text()); } else { error = result.error(); }
//   });
//
//   const http::Fetch profile("https://api.example.com/me");     // reactive: loads when made
//   If([=] { return profile.is_loading(); }, ProgressView(), ...)
//   Text([=] { return profile.ok() ? profile.response().text() : profile.error(); })

#include <elg/export.h>
#include <elg/reactive.h>

#include <chrono>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace elg::http {

using Headers = std::vector<std::pair<std::string, std::string>>;

struct Request {
    std::string url;
    std::string method = "GET";
    Headers headers;
    std::string body; // sent as is; set a Content-Type header to go with it
    std::chrono::milliseconds timeout{30'000};
};

struct Response {
    int status = 0;
    Headers headers;
    std::vector<std::uint8_t> body;

    bool ok() const noexcept { return status >= 200 && status < 300; }
    std::string text() const { return std::string(body.begin(), body.end()); }
    // The first header with this name (case-insensitive), or empty.
    ELG_API std::string header(std::string_view name) const;
};

// A response, or why there isn't one (network, TLS, timeout, cancelled). An HTTP error status is
// still a response: check ok().
using Result = std::expected<Response, std::string>;

// A request in flight. Copies refer to the same request.
class ELG_API Task {
public:
    Task() = default;
    // Stops the request; its callback won't run.
    void cancel() const;

    struct State;
    // Internal: made by fetch().
    explicit Task(std::shared_ptr<State> state) noexcept : state_(std::move(state)) {}

private:
    ELG_PRIVATE_MEMBERS_BEGIN
    std::shared_ptr<State> state_;
    ELG_PRIVATE_MEMBERS_END
};

// Starts a request; `done` runs on the UI thread with the result (or not at all if cancelled, or
// if no App is running by then). Call from any thread.
ELG_API Task fetch(Request request, std::function<void(const Result&)> done);

// A request as reactive state: it starts when made, and views reading it update when it
// finishes. Copies share the request; it's cancelled when the last copy goes. UI thread.
class ELG_API Fetch {
public:
    explicit Fetch(Request request);
    explicit Fetch(std::string url) : Fetch(Request{.url = std::move(url)}) {}

    bool is_loading() const;
    // Finished with a response (whatever its status).
    bool has_response() const;
    // Finished with a 2xx response.
    bool ok() const;
    // The response; empty (status 0) until there is one.
    Response response() const;
    // Why it failed (no response, or a non-2xx status); empty otherwise.
    std::string error() const;
    // Starts it again (the old result stays until the new one arrives).
    void reload() const;

    struct State;

private:
    ELG_PRIVATE_MEMBERS_BEGIN
    std::shared_ptr<State> state_;
    ELG_PRIVATE_MEMBERS_END
};

} // namespace elg::http

#endif // ELG_HTTP_H
