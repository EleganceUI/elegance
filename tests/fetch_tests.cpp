// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// AsyncImage's downloads (net/fetch.h) against a local server: plain HTTP, and HTTPS with a test
// certificate authority, so certificate and host name checks are exercised without the network.

#include "net/fetch.h"
#include "net/http_cache.h"
#include "net/proxy.h"

#include "core/log.h"
#include "core/ui_tasks.h"
#include "test_images.h"
#include "view/view_node.h"

#include <elg/elegance.h>

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <atomic>
#include <array>
#include <filesystem>
#include <fstream>
#include <future>
#include <map>
#include <mutex>
#include <random>
#include <string>
#include <string_view>
#include <thread>

using namespace elg;
using namespace std::chrono_literals;
using Catch::Matchers::ContainsSubstring;

namespace {

namespace asio = boost::asio;
namespace bhttp = boost::beast::http;
namespace ssl = boost::asio::ssl;
namespace net = elg::detail::net;
using asio::awaitable;
using asio::use_awaitable;

// "Elegance Test CA", and a certificate it signed for "localhost" only (valid until 2126).
constexpr const char* kTestAuthority = R"(-----BEGIN CERTIFICATE-----
MIIBnTCCAUOgAwIBAgIUOo2r3UbhKa9FQIYKgSL8+OCkaMcwCgYIKoZIzj0EAwIw
GzEZMBcGA1UEAwwQRWxlZ2FuY2UgVGVzdCBDQTAgFw0yNjEwMjcwNDExMThaGA8y
MTI2MTAwMzA0MTExOFowGzEZMBcGA1UEAwwQRWxlZ2FuY2UgVGVzdCBDQTBZMBMG
ByqGSM49AgEGCCqGSM49AwEHA0IABDeHmzmbmFZlda47296tDwWP4o5A4GgucafI
S9Ix1BEHxBOBjIjRjJcoYN+dZr7tPtqiGq5vsaW6qBxqONGTFOSjYzBhMB0GA1Ud
DgQWBBR7Ms0aJy15Y81iuVoAhO9oNZJqzzAfBgNVHSMEGDAWgBR7Ms0aJy15Y81i
uVoAhO9oNZJqzzAPBgNVHRMBAf8EBTADAQH/MA4GA1UdDwEB/wQEAwICBDAKBggq
hkjOPQQDAgNIADBFAiEAhViH/X1MdpRTwWqviDzqkRqNQEwNwJp4EJK9LncAY4kC
IAw7s5XCL1ubYamVUeakJwPlN7yMt6vg+7HoWERjImpf
-----END CERTIFICATE-----
)";

constexpr const char* kLocalhostCertificate = R"(-----BEGIN CERTIFICATE-----
MIIBqzCCAVGgAwIBAgIUNmszr+/+jCNGBN2dySd2/EzIPPgwCgYIKoZIzj0EAwIw
GzEZMBcGA1UEAwwQRWxlZ2FuY2UgVGVzdCBDQTAgFw0yNjEwMjcwNDExMThaGA8y
MTI2MTAwMzA0MTExOFowFDESMBAGA1UEAwwJbG9jYWxob3N0MFkwEwYHKoZIzj0C
AQYIKoZIzj0DAQcDQgAE8FL3r1GDUthM2/XdMKC7YbB1j06ThMJmQLo7XkgBCqZi
wrAhFSfGfPOIX3vlNFmAvRz4hy3Lt59QHxjR8npDRqN4MHYwFAYDVR0RBA0wC4IJ
bG9jYWxob3N0MAkGA1UdEwQCMAAwEwYDVR0lBAwwCgYIKwYBBQUHAwEwHQYDVR0O
BBYEFP2882CnySfUICoRufiCD5+BXXFAMB8GA1UdIwQYMBaAFHsyzRonLXljzWK5
WgCE72g1kmrPMAoGCCqGSM49BAMCA0gAMEUCIEve9DIO+M5WVXiAfFmtAHSk+OzG
SQ8481xx4e0ovBYUAiEApO0Vc3gxPjDVewICfnzIXZyRjorFE1fAffd1McOunq8=
-----END CERTIFICATE-----
)";

constexpr const char* kLocalhostKey = R"(-----BEGIN PRIVATE KEY-----
MIGHAgEAMBMGByqGSM49AgEGCCqGSM49AwEHBG0wawIBAQQgAqGBceVkpS81kkTd
47u2foYHaKS4FndBS6I28yI1yC6hRANCAATwUvevUYNS2Ezb9d0woLthsHWPTpOE
wmZAujteSAEKpmLCsCEVJ8Z884hfe+U0WYC9HPiHLcu3n1AfGNHyekNG
-----END PRIVATE KEY-----
)";

// Serves on 127.0.0.1, on its own thread:
//   /hello      200 "hello"
//   /missing    404
//   /redirect   302 to /hello
//   /loop       302 to itself
//   /http       302 to the same path over plain http (from the TLS server: a downgrade)
//   /big        200 with 4096 bytes
//   /slow       answers after a minute
//   /cached     200, fresh for a minute
//   /revalidate 200 "fresh" with an ETag, revalidated every time (304 when it matches)
//   /stale      200, stale at once, with an ETag
//   /nostore    200, not to be stored
//   /two-fields 200 with Cache-Control in two fields, together "don't store"
//   /header-60k 200 with a header section of about 60 KiB
//   /header-70k 200 with a header section of about 70 KiB
//   /huge.png   200 with a PNG file of 16384 x 8193 pixels (in 106 KB)
//   /small.png  200 with a PNG file of 64 x 32 pixels
//   /aged       200, fresh for 600 s but already 600 s old, with an ETag; its 304 has no Age
//   /gone-on-304 200 "kept", revalidated every time, with an ETag; its 304 says no-store
//   /latest     302 to /v123.png, with no lifetime of its own
//   /v123.png   200 "v123", fresh for a year
//   /moved      302 to /hello, fresh for a minute
//   /permanent  301 to /hello, with no lifetime of its own
//   /stale-redirect 302 to /stale, stale at once
// Connections stay open for more requests, unless `drop` (closed after each response, without
// saying so).
class TestServer {
public:
    explicit TestServer(bool secure = false, bool drop = false) : secure_(secure), drop_(drop) {
        if (secure_) {
            tls_.use_certificate_chain(asio::buffer(std::string_view(kLocalhostCertificate)));
            tls_.use_private_key(asio::buffer(std::string_view(kLocalhostKey)), ssl::context::pem);
        }
        asio::co_spawn(io_, listen(), asio::detached);
        thread_ = std::thread([this] { io_.run(); });
    }

    TestServer(const TestServer&) = delete;
    TestServer(TestServer&&) = delete;
    TestServer& operator=(const TestServer&) = delete;
    TestServer& operator=(TestServer&&) = delete;
    ~TestServer() {
        io_.stop();
        thread_.join();
    }

    std::string url(std::string_view path, std::string_view host = "127.0.0.1") const {
        return std::format("{}://{}:{}{}", secure_ ? "https" : "http", host, acceptor_.local_endpoint().port(), path);
    }
    int connections() const { return connections_; }
    int hits(const std::string& path) const {
        const std::lock_guard lock(mutex_);
        const auto it = hits_.find(path);
        return it == hits_.end() ? 0 : it->second;
    }
    int not_modified() const { return not_modified_; }

private:
    // Coroutines co_awaited right away by their caller, which owns what they refer to.
    // NOLINTBEGIN(cppcoreguidelines-avoid-reference-coroutine-parameters)
    template <class Stream>
    awaitable<void> serve(Stream& stream) {
        boost::beast::flat_buffer buffer;
        while (true) {
            bhttp::request<bhttp::string_body> request;
            co_await bhttp::async_read(stream, buffer, request, use_awaitable);
            auto response = respond(request);
            co_await serve_slowly(request);
            // Through a serializer: the overload that takes the message has a default argument GCC
            // reports as a zero null pointer at each call (-Wzero-as-null-pointer-constant).
            bhttp::response_serializer<bhttp::string_body> serializer{response};
            co_await bhttp::async_write(stream, serializer, use_awaitable);
            if (drop_ || !response.keep_alive()) {
                co_return;
            }
        }
    }

    static awaitable<void> serve_slowly(const bhttp::request<bhttp::string_body>& request) {
        if (request.target() == "/slow") {
            asio::steady_timer timer(co_await asio::this_coro::executor, 60s);
            co_await timer.async_wait(use_awaitable);
        }
    }
    // NOLINTEND(cppcoreguidelines-avoid-reference-coroutine-parameters)

    bhttp::response<bhttp::string_body> respond(const bhttp::request<bhttp::string_body>& request) {
        bhttp::response<bhttp::string_body> response{bhttp::status::ok, 11};
        const std::string target(request.target());
        {
            const std::lock_guard lock(mutex_);
            ++hits_[target];
        }
        if (target == "/hello") {
            response.body() = "hello";
        } else if (target == "/redirect") {
            response.result(bhttp::status::found);
            response.set(bhttp::field::location, "/hello");
        } else if (target == "/loop") {
            response.result(bhttp::status::found);
            response.set(bhttp::field::location, "/loop");
        } else if (target == "/http") {
            response.result(bhttp::status::found);
            response.set(bhttp::field::location, std::format("http://127.0.0.1:{}/hello", acceptor_.local_endpoint().port()));
        } else if (target == "/big") {
            response.body() = std::string(4096, 'x');
        } else if (target == "/slow") {
            response.body() = "slow";
        } else if (target == "/cached") {
            response.set(bhttp::field::cache_control, "max-age=60");
            response.body() = "cached";
        } else if (target == "/revalidate") {
            response.set(bhttp::field::cache_control, "no-cache");
            response.set(bhttp::field::etag, "\"v1\"");
            if (request[bhttp::field::if_none_match] == "\"v1\"") {
                response.result(bhttp::status::not_modified);
                ++not_modified_;
            } else {
                response.body() = "fresh";
            }
        } else if (target == "/stale") {
            response.set(bhttp::field::cache_control, "max-age=0");
            response.set(bhttp::field::etag, "\"s\"");
            response.body() = "stale";
        } else if (target == "/nostore") {
            response.set(bhttp::field::cache_control, "no-store");
            response.set(bhttp::field::etag, "\"n\"");
            response.body() = "secret";
        } else if (target == "/two-fields") {
            // Two Cache-Control fields: together they mean "don't store".
            response.insert(bhttp::field::cache_control, "max-age=600");
            response.insert(bhttp::field::cache_control, "no-store");
            response.body() = "split";
        } else if (target == "/header-60k" || target == "/header-70k") {
            // In fields of 10 KiB: Beast refuses to hold a single field value of 64 KiB or more.
            for (int i = 0; i < (target == "/header-60k" ? 6 : 7); ++i) {
                response.insert(std::format("X-Padding-{}", i), std::string(std::size_t{10} * 1024, 'p'));
            }
            response.body() = "padded";
        } else if (target == "/aged") {
            response.set(bhttp::field::etag, "\"a\"");
            if (request[bhttp::field::if_none_match] == "\"a\"") {
                response.result(bhttp::status::not_modified); // no Cache-Control or Age
                ++not_modified_;
            } else {
                response.set(bhttp::field::cache_control, "max-age=600");
                response.set(bhttp::field::age, "600");
                response.body() = "aged";
            }
        } else if (target == "/gone-on-304") {
            response.set(bhttp::field::etag, "\"g\"");
            if (request[bhttp::field::if_none_match] == "\"g\"") {
                response.result(bhttp::status::not_modified);
                response.set(bhttp::field::cache_control, "no-store");
                ++not_modified_;
            } else {
                response.set(bhttp::field::cache_control, "no-cache");
                response.body() = "kept";
            }
        } else if (target == "/latest" || target == "/moved" || target == "/permanent" ||
                   target == "/stale-redirect") {
            response.result(target == "/permanent" ? bhttp::status::moved_permanently : bhttp::status::found);
            response.set(bhttp::field::location, target == "/latest"           ? "/v123.png"
                                                 : target == "/stale-redirect" ? "/stale"
                                                                               : "/hello");
            if (target == "/moved") {
                response.set(bhttp::field::cache_control, "max-age=60");
            } else if (target == "/stale-redirect") {
                response.set(bhttp::field::cache_control, "max-age=0");
            }
        } else if (target == "/v123.png") {
            response.set(bhttp::field::cache_control, "max-age=31536000");
            response.body() = "v123";
        } else if (target == "/small.png") {
            const std::vector<std::uint8_t> png = test::black_png(64, 32);
            response.body().assign(png.begin(), png.end());
        } else if (target == "/huge.png") {
            const std::vector<std::uint8_t> png = test::black_png(16384, 8193);
            response.body().assign(png.begin(), png.end());
        } else {
            response.result(bhttp::status::not_found);
        }
        response.keep_alive(request.keep_alive());
        response.prepare_payload();
        return response;
    }

    awaitable<void> session(asio::ip::tcp::socket socket) {
        try {
            if (secure_) {
                ssl::stream<asio::ip::tcp::socket> stream(std::move(socket), tls_);
                co_await stream.async_handshake(ssl::stream_base::server, use_awaitable);
                co_await serve(stream);
            } else {
                co_await serve(socket);
            }
        } catch (const std::exception&) {
            // Swallowed: the client gave up (a refused certificate, a cancelled request).
        }
    }

    awaitable<void> listen() {
        while (true) {
            auto socket = co_await acceptor_.async_accept(use_awaitable);
            ++connections_;
            asio::co_spawn(io_, session(std::move(socket)), asio::detached);
        }
    }

    bool secure_;
    bool drop_;
    std::atomic<int> connections_ = 0;
    std::atomic<int> not_modified_ = 0;
    mutable std::mutex mutex_;
    std::map<std::string, int> hits_;
    asio::io_context io_;
    asio::ip::tcp::acceptor acceptor_{io_, {asio::ip::make_address("127.0.0.1"), 0}};
    ssl::context tls_{ssl::context::tls_server};
    std::thread thread_;
};

// An HTTP proxy on 127.0.0.1: plain requests (absolute-form) are answered "via proxy <url>";
// CONNECT tunnels to the target. With `password`, it wants Basic "user:<password>".
class TestProxy {
public:
    // `basic`: "user:<password>" in base64.
    explicit TestProxy(std::string password = {}, std::string basic = {})
        : password_(std::move(password)), basic_(std::move(basic)) {
        asio::co_spawn(io_, listen(), asio::detached);
        thread_ = std::thread([this] { io_.run(); });
    }

    TestProxy(const TestProxy&) = delete;
    TestProxy(TestProxy&&) = delete;
    TestProxy& operator=(const TestProxy&) = delete;
    TestProxy& operator=(TestProxy&&) = delete;
    ~TestProxy() {
        io_.stop();
        thread_.join();
    }

    [[nodiscard]] std::string url(std::string_view credentials = {}) const {
        return std::format("http://{}127.0.0.1:{}", credentials, acceptor_.local_endpoint().port());
    }

private:
    static awaitable<void> pipe(std::shared_ptr<asio::ip::tcp::socket> from, std::shared_ptr<asio::ip::tcp::socket> to) {
        std::array<char, 4096> data{};
        try {
            while (true) {
                const std::size_t size = co_await from->async_read_some(asio::buffer(data), use_awaitable);
                co_await asio::async_write(*to, asio::buffer(data.data(), size), use_awaitable);
            }
        } catch (const std::exception&) {
            boost::system::error_code ignored;
            static_cast<void>(to->shutdown(asio::ip::tcp::socket::shutdown_send, ignored));
        }
    }

    awaitable<void> session(asio::ip::tcp::socket accepted) {
        auto client = std::make_shared<asio::ip::tcp::socket>(std::move(accepted));
        try {
            boost::beast::flat_buffer buffer;
            bhttp::request<bhttp::empty_body> request;
            co_await bhttp::async_read(*client, buffer, request, use_awaitable);
            const std::string expected = password_.empty() ? std::string() : "Basic " + basic_;
            if (!expected.empty() && request[bhttp::field::proxy_authorization] != expected) {
                bhttp::response<bhttp::empty_body> refusal{bhttp::status::proxy_authentication_required, 11};
                refusal.keep_alive(false);
                bhttp::response_serializer<bhttp::empty_body> serializer{refusal}; // as in serve()
                co_await bhttp::async_write(*client, serializer, use_awaitable);
                co_return;
            }
            if (request.method() != bhttp::verb::connect) {
                bhttp::response<bhttp::string_body> response{bhttp::status::ok, 11};
                response.body() = "via proxy " + std::string(request.target());
                response.keep_alive(false);
                response.prepare_payload();
                bhttp::response_serializer<bhttp::string_body> serializer{response};
                co_await bhttp::async_write(*client, serializer, use_awaitable);
                co_return;
            }
            const std::string authority(request.target());
            const std::size_t colon = authority.rfind(':');
            asio::ip::tcp::resolver resolver(io_);
            auto server = std::make_shared<asio::ip::tcp::socket>(io_);
            co_await asio::async_connect(*server,
                                         co_await resolver.async_resolve(authority.substr(0, colon),
                                                                         authority.substr(colon + 1), use_awaitable),
                                         use_awaitable);
            const std::string_view established = "HTTP/1.1 200 Connection established\r\n\r\n";
            co_await asio::async_write(*client, asio::buffer(established), use_awaitable);
            asio::co_spawn(io_, pipe(client, server), asio::detached);
            asio::co_spawn(io_, pipe(server, client), asio::detached);
        } catch (const std::exception&) {
            // Swallowed: the client went away before the tunnel was set up.
        }
    }

    awaitable<void> listen() {
        while (true) {
            auto socket = co_await acceptor_.async_accept(use_awaitable);
            asio::co_spawn(io_, session(std::move(socket)), asio::detached);
        }
    }

    std::string password_;
    std::string basic_;
    asio::io_context io_;
    asio::ip::tcp::acceptor acceptor_{io_, {asio::ip::make_address("127.0.0.1"), 0}};
    std::thread thread_;
};

// A fresh directory for a disk cache, removed afterwards.
struct TemporaryDirectory {
    std::filesystem::path path;
    TemporaryDirectory() {
        std::random_device random;
        path = std::filesystem::temp_directory_path() / std::format("elg-cache-test-{:x}", random());
        std::filesystem::create_directories(path);
    }
    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory(TemporaryDirectory&&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(TemporaryDirectory&&) = delete;
    ~TemporaryDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
    // Stored entries (not the temporary files of stores under way).
    [[nodiscard]] std::size_t files() const {
        std::size_t count = 0;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(path)) {
            count += entry.is_regular_file() && entry.path().extension() != ".tmp";
        }
        return count;
    }
};

// Stores happen in the background; waits for one.
bool wait_until_cached(net::HttpCache& cache, const std::string& url) {
    for (int i = 0; i < 200; ++i) {
        if (cache.find(url)) {
            return true;
        }
        std::this_thread::sleep_for(10ms);
    }
    return false;
}

net::FetchResult fetch(std::string url, net::FetchOptions options = {}) {
    std::promise<net::FetchResult> promise;
    auto future = promise.get_future();
    net::fetch(std::move(url), [&](net::FetchResult result) { promise.set_value(std::move(result)); }, std::move(options));
    REQUIRE(future.wait_for(10s) == std::future_status::ready);
    return future.get();
}

// Shows an AsyncImage of `url` (memory cache only), and runs the UI thread's tasks until it leaves
// the loading phase.
AsyncImagePhase load_async_image(const std::string& url, AsyncImageOptions options = {.disk_cache = false}) {
    detail::Scope scope;
    const detail::Scope::Use use(scope);
    AsyncImagePhase last;
    const View view = AsyncImage(
        url,
        [&](const AsyncImagePhase& phase) {
            last = phase;
            return View();
        },
        options);
    const std::unique_ptr<detail::Element> root = detail::lower(view, {});
    for (int i = 0; i < 1000 && last.kind == AsyncImagePhase::Kind::loading; ++i) {
        std::this_thread::sleep_for(10ms);
        detail::run_ui_tasks();
        detail::flush_effects();
    }
    return last;
}

// Whether a recent log line has `text` in it.
bool logged(std::string_view text) {
    return std::ranges::any_of(detail::recent_log_lines(), [&](const std::string& line) { return line.contains(text); });
}

std::string text(const net::FetchResult& result) {
    INFO((result.has_value() ? std::string() : result.error())); // why it failed, if it did
    REQUIRE(result.has_value());
    return {result->begin(), result->end()};
}

} // namespace

TEST_CASE("downloads follow redirects and report HTTP errors", "[fetch]") {
    const TestServer server;
    CHECK(text(fetch(server.url("/hello"))) == "hello");
    CHECK(text(fetch(server.url("/redirect"))) == "hello");
    CHECK(fetch(server.url("/missing")).error() == "HTTP 404");
    CHECK(text(fetch(server.url("/big"))).size() == 4096);
}

TEST_CASE("downloads name the limit they exceeded", "[fetch]") {
    const TestServer server;
    CHECK(fetch(server.url("/loop")).error() == "too many redirects (more than 5)");
    CHECK(fetch(server.url("/loop"), {.max_redirects = 2}).error() == "too many redirects (more than 2)");
    CHECK(fetch(server.url("/big"), {.max_body = 1024}).error() == "the response is larger than 1 KiB");
    CHECK(fetch(server.url("/big"), {.max_body = 1000}).error() == "the response is larger than 1000 bytes");
}

TEST_CASE("downloads refuse a response whose header section is over 64 KiB", "[fetch]") {
    const TestServer server;
    CHECK(text(fetch(server.url("/header-60k"))) == "padded");
    CHECK(fetch(server.url("/header-70k")).error() == "the response's header section is larger than 64 KiB");
    // The connection is closed, not left with the rest of that response: the next request works.
    CHECK(text(fetch(server.url("/hello"))) == "hello");
}

TEST_CASE("downloads refuse what isn't an http(s) URL", "[fetch]") {
    CHECK(fetch("file:///etc/hosts").error() == "not an http(s) URL");
    CHECK(fetch("not a url").error() == "not an http(s) URL");
    CHECK(fetch("https://").error() == "not an http(s) URL");
}

TEST_CASE("downloads time out", "[fetch]") {
    const TestServer server;
    CHECK(fetch(server.url("/slow"), {.timeout = 200ms}).error() == "the request timed out (after 200 ms)");
}

TEST_CASE("cancelled downloads don't call back", "[fetch]") {
    const TestServer server;
    std::atomic<bool> called = false;
    const net::FetchTask task = net::fetch(server.url("/slow"), [&](net::FetchResult) { called = true; });
    std::this_thread::sleep_for(50ms);
    task.cancel();
    task.cancel(); // twice is fine
    CHECK_NOTHROW(net::FetchTask().cancel()); // an empty task
    // The network thread is still running: a later download completes, and the cancelled one stayed quiet.
    CHECK(text(fetch(server.url("/hello"))) == "hello");
    CHECK_FALSE(called);
}

TEST_CASE("a download cancelled just as it ends doesn't call back", "[fetch]") {
    const TestServer server;
    std::promise<net::FetchTask> task;
    std::promise<void> cancelled;
    std::atomic<bool> called = false;
    // The download is over and its callback is next when the hook cancels it: cancel() returns
    // before the callback could start, so it mustn't.
    net::FetchOptions options;
    options.before_callback = [&, shared = task.get_future().share()] {
        shared.get().cancel();
        cancelled.set_value();
    };
    task.set_value(net::fetch(server.url("/hello"), [&](net::FetchResult) { called = true; }, std::move(options)));
    REQUIRE(cancelled.get_future().wait_for(10s) == std::future_status::ready);
    CHECK(text(fetch(server.url("/hello"))) == "hello"); // after the network thread is done with it
    CHECK_FALSE(called);
}

TEST_CASE("cancelling doesn't wait for a callback that has started", "[fetch]") {
    const TestServer server;
    std::promise<void> started;
    std::promise<void> release;
    const std::shared_future<void> released = release.get_future().share();
    const net::FetchTask task = net::fetch(server.url("/hello"), [&, released](net::FetchResult) {
        started.set_value();
        released.wait();
    });
    REQUIRE(started.get_future().wait_for(10s) == std::future_status::ready);
    task.cancel(); // returns while the callback waits for it
    release.set_value();
    CHECK(text(fetch(server.url("/hello"))) == "hello");
}

TEST_CASE("https checks the certificate and the host name", "[fetch]") {
    const TestServer server(true);
    // Signed by an authority the system doesn't trust.
    CHECK(fetch(server.url("/hello", "localhost")).error().starts_with("the secure connection failed"));
    // Trusted, and issued to this host.
    CHECK(text(fetch(server.url("/hello", "localhost"), {.root_certificates = kTestAuthority})) == "hello");
    // Trusted, but issued to "localhost", not "127.0.0.1".
    CHECK(fetch(server.url("/hello"), {.root_certificates = kTestAuthority}).error().starts_with("the secure connection failed"));
    // Never from https down to http.
    CHECK(fetch(server.url("/http", "localhost"), {.root_certificates = kTestAuthority}).error() ==
          "the server redirected from https to http");
}

// Hidden (run with "[.network]"): the system's root certificates against a real server.
TEST_CASE("https trusts the system's root certificates", "[.network]") {
    const net::FetchResult result = fetch("https://example.com/");
    INFO((result ? std::string() : result.error()));
    CHECK(result.has_value());
}

TEST_CASE("connections are kept open and reused", "[fetch]") {
    const TestServer server;
    for (int i = 0; i < 3; ++i) {
        CHECK(text(fetch(server.url("/hello"))) == "hello");
    }
    CHECK(server.connections() == 1);

    // Over TLS too (one handshake).
    const TestServer secure(true);
    for (int i = 0; i < 3; ++i) {
        CHECK(text(fetch(secure.url("/hello", "localhost"), {.root_certificates = kTestAuthority})) == "hello");
    }
    CHECK(secure.connections() == 1);
}

TEST_CASE("a kept connection the server closed is replaced", "[fetch]") {
    const TestServer server(false, true); // closes after each response without saying so
    for (int i = 0; i < 3; ++i) {
        CHECK(text(fetch(server.url("/hello"))) == "hello");
    }
    CHECK(server.connections() == 3);
}

TEST_CASE("downloads go through an HTTP proxy", "[fetch][proxy]") {
    const TestServer server(true);
    const TestProxy proxy;
    // Plain http: the whole URL goes to the proxy.
    CHECK(text(fetch("http://example.invalid/a?b", {.proxy = proxy.url()})) == "via proxy http://example.invalid:80/a?b");
    // https: through a tunnel; the certificate is still checked against the real host.
    CHECK(text(fetch(server.url("/hello", "localhost"), {.root_certificates = kTestAuthority, .proxy = proxy.url()})) ==
          "hello");
    CHECK(fetch(server.url("/hello", "localhost"), {.proxy = proxy.url()}).error().starts_with("the secure connection failed"));
    CHECK(fetch(server.url("/hello"), {.proxy = "socks5://127.0.0.1:1"}).error().starts_with("the proxy isn't supported"));
    CHECK(fetch(server.url("/hello"), {.proxy = "http://127.0.0.1:1"}).error().starts_with("couldn't connect to the proxy"));
}

TEST_CASE("a proxy's password goes with the tunnel", "[fetch][proxy]") {
    const TestServer server(true);
    const TestProxy proxy("secret", "dXNlcjpzZWNyZXQ="); // user:secret
    const auto through = [&](std::string_view credentials) {
        return fetch(server.url("/hello", "localhost"),
                     {.root_certificates = kTestAuthority, .proxy = proxy.url(credentials)});
    };
    CHECK(text(through("user:secret@")) == "hello");
    CHECK(through("").error() == "the proxy asks for a password");
    CHECK(through("user:wrong@").error() == "the proxy didn't accept its password");
}

TEST_CASE("proxy settings: URLs, NO_PROXY, the environment, Windows' lists", "[proxy]") {
    const net::ProxyChoice full = net::parse_proxy_url("http://us%20er:p%40ss@proxy.corp:8080");
    REQUIRE(full);
    REQUIRE(full->has_value());
    // NOLINTBEGIN(bugprone-unchecked-optional-access): both proxies are REQUIREd before they're read
    CHECK((*full)->host == "proxy.corp");
    CHECK((*full)->port == "8080");
    CHECK((*full)->authorization == "Basic dXMgZXI6cEBzcw=="); // "us er:p@ss"
    const net::ProxyChoice bare = net::parse_proxy_url("proxy.corp");
    REQUIRE((bare && bare->has_value()));
    CHECK((*bare)->port == "1080");
    CHECK((*bare)->authorization.empty());
    // NOLINTEND(bugprone-unchecked-optional-access)
    CHECK_FALSE(net::parse_proxy_url("https://proxy.corp:443"));
    CHECK_FALSE(net::parse_proxy_url("socks5://proxy.corp:1080"));

    CHECK(net::bypasses_proxy("example.com", "example.com", "443"));
    CHECK(net::bypasses_proxy("example.com", "img.example.com", "443"));
    CHECK(net::bypasses_proxy(".example.com", "img.example.com", "443"));
    CHECK(net::bypasses_proxy("*.example.com", "img.example.com", "443"));
    CHECK_FALSE(net::bypasses_proxy("example.com", "notexample.com", "443"));
    CHECK(net::bypasses_proxy("other.org, example.com:8443", "example.com", "8443"));
    CHECK_FALSE(net::bypasses_proxy("example.com:8443", "example.com", "443"));
    CHECK(net::bypasses_proxy("*", "anything", "80"));
    CHECK(net::bypasses_proxy("[::2]", "[::2]", "80"));

    std::map<std::string, std::string> environment;
    const auto get = [&](const char* name) -> std::optional<std::string> {
        const auto it = environment.find(name);
        return it == environment.end() ? std::nullopt : std::optional(it->second);
    };
    CHECK_FALSE(net::proxy_from_environment("https", "example.com", "443", get)); // the system's settings then
    environment["HTTPS_PROXY"] = "http://secure.corp:3128";
    environment["HTTP_PROXY"] = "http://ignored.corp:3128"; // CGI servers set this from a request header
    const auto https = net::proxy_from_environment("https", "example.com", "443", get);
    REQUIRE((https && *https && https->value()));
    CHECK(https->value()->host == "secure.corp"); // NOLINT(bugprone-unchecked-optional-access): REQUIREd above
    CHECK_FALSE(net::proxy_from_environment("http", "example.com", "80", get));
    environment["http_proxy"] = "plain.corp:8080";
    const auto http = net::proxy_from_environment("http", "example.com", "80", get);
    REQUIRE((http && *http && http->value()));
    CHECK(http->value()->host == "plain.corp"); // NOLINT(bugprone-unchecked-optional-access): REQUIREd above
    environment["NO_PROXY"] = "example.com";
    const auto bypassed = net::proxy_from_environment("https", "example.com", "443", get);
    REQUIRE((bypassed && *bypassed));
    CHECK_FALSE(bypassed->value().has_value()); // NOLINT(bugprone-unchecked-optional-access): REQUIREd above

    const auto windows = [](std::string_view proxies, std::string_view bypass, std::string_view scheme,
                            std::string_view host) -> std::string {
        const net::ProxyChoice choice = net::proxy_from_windows_settings(proxies, bypass, scheme, host);
        if (!choice) {
            return "error";
        }
        return *choice ? (*choice)->host + ":" + (*choice)->port : "direct";
    };
    CHECK(windows("proxy:8080", "", "https", "example.com") == "proxy:8080");
    CHECK(windows("http=web:80;https=secure:443", "", "https", "example.com") == "secure:443");
    CHECK(windows("http=web:80;https=secure:443", "", "http", "example.com") == "web:80");
    CHECK(windows("http=web:80", "", "https", "example.com") == "direct");
    CHECK(windows("proxy:8080", "*.corp;<local>", "https", "intranet") == "direct");
    CHECK(windows("proxy:8080", "*.corp;<local>", "https", "wiki.corp") == "direct");
    CHECK(windows("proxy:8080", "*.corp;<local>", "https", "example.com") == "proxy:8080");

    CHECK(net::is_loopback("localhost"));
    CHECK(net::is_loopback("127.0.0.9"));
    CHECK(net::is_loopback("[::1]"));
    CHECK_FALSE(net::is_loopback("example.com"));
    // Loopback hosts never go through a proxy, whatever the settings.
    const net::ProxyChoice local = net::find_proxy("http", "127.0.0.1", "80");
    REQUIRE(local);
    CHECK_FALSE(local->has_value());
}

TEST_CASE("bypass patterns match in time proportional to pattern times host", "[proxy]") {
    CHECK(net::wildcard_match("", ""));
    CHECK(net::wildcard_match("*", ""));
    CHECK(net::wildcard_match("**", "a"));
    CHECK(net::wildcard_match("*.corp", "wiki.corp"));
    CHECK(net::wildcard_match("w*i*.corp", "wiki.corp"));
    CHECK(net::wildcard_match("*a*b", "aXbYab"));
    CHECK(net::wildcard_match("a*", "a"));
    CHECK_FALSE(net::wildcard_match("", "a"));
    CHECK_FALSE(net::wildcard_match("*.corp", "corp"));
    CHECK_FALSE(net::wildcard_match("a*b", "aXbY"));
    CHECK_FALSE(net::wildcard_match("wiki", "wiki.corp"));

    // A backtracking matcher tries the ways to split the host among the stars: about 10^23 here.
    std::string pattern;
    for (int i = 0; i < 16; ++i) {
        pattern += "*a";
    }
    pattern += "*b";
    const std::string host(200, 'a');
    CHECK_FALSE(net::wildcard_match(pattern, host));
    CHECK(net::wildcard_match(pattern, host + "b"));
    // Through Windows' bypass list, which is where such patterns come from.
    const net::ProxyChoice choice = net::proxy_from_windows_settings("proxy:8080", pattern, "https", host + ".com");
    REQUIRE(choice);
    CHECK(choice->has_value());
}

TEST_CASE("HTTP dates and freshness", "[cache]") {
    constexpr std::int64_t kNov6 = 784111777; // Sun, 06 Nov 1994 08:49:37 GMT
    CHECK(net::parse_http_date("Sun, 06 Nov 1994 08:49:37 GMT") == kNov6);
    CHECK(net::parse_http_date("Sunday, 06-Nov-94 08:49:37 GMT") == kNov6);
    CHECK(net::parse_http_date("Sun Nov  6 08:49:37 1994") == kNov6);
    CHECK_FALSE(net::parse_http_date("0"));
    CHECK_FALSE(net::parse_http_date("Sun, 31 Feb 1994 08:49:37 GMT"));

    const std::int64_t now = kNov6;
    const auto entry = [&](const net::CacheHeaders& headers) {
        return net::make_cache_entry("https://example.com/a.png", 200, {}, headers, {1, 2, 3}, now);
    };
    // NOLINTBEGIN(bugprone-unchecked-optional-access): each entry is REQUIREd before it's read
    const auto max_age = entry({.cache_control = "public, max-age=60"});
    REQUIRE(max_age);
    CHECK(max_age->fresh(now + 59));
    CHECK_FALSE(max_age->fresh(now + 60));
    // Age counts against it; so does a Date in the past.
    CHECK_FALSE(entry({.cache_control = "max-age=60", .age = "60"}));
    // Ages past 2^31, even too large to parse, count as 2^31 (RFC 9111): old, never fresh.
    CHECK_FALSE(entry({.cache_control = "max-age=60", .age = "99999999999999999999"}));
    const auto ancient = entry({.cache_control = "max-age=60", .age = "9223372036854775807", .etag = "\"e\""});
    REQUIRE(ancient);
    CHECK_FALSE(ancient->fresh(now + 1));
    const auto aged = entry({.cache_control = "max-age=60", .age = "30", .etag = "\"e\""});
    REQUIRE(aged);
    CHECK_FALSE(aged->fresh(now + 30));
    // Expires, from the server's Date.
    const auto expires = entry({.expires = "Sun, 06 Nov 1994 08:59:37 GMT", .date = "Sun, 06 Nov 1994 08:49:37 GMT"});
    REQUIRE(expires);
    CHECK(expires->lifetime == 600);
    CHECK_FALSE(entry({.expires = "0"})); // already expired, and nothing to revalidate with
    // A tenth of the time since Last-Modified, at most a day.
    const auto guessed = entry({.date = "Sun, 06 Nov 1994 08:49:37 GMT", .last_modified = "Sun, 06 Nov 1994 07:49:37 GMT"});
    REQUIRE(guessed);
    CHECK(guessed->lifetime == 360);
    CHECK(guessed->revalidates());
    // Not stored.
    CHECK_FALSE(entry({.cache_control = "no-store", .etag = "\"e\""}));
    CHECK_FALSE(entry({.cache_control = "max-age=60", .vary = "*"}));
    CHECK_FALSE(entry({})); // never fresh, no validators
    // Stored, but revalidated every time.
    const auto no_cache = entry({.cache_control = "no-cache", .etag = "\"e\""});
    REQUIRE(no_cache);
    CHECK_FALSE(no_cache->fresh(now));
    CHECK_FALSE(no_cache->usable_stale());
    // A 304 renews it.
    net::CacheEntry renewed = *aged;
    // NOLINTEND(bugprone-unchecked-optional-access)
    net::refresh_cache_entry(renewed, {.cache_control = "max-age=60", .age = "0"}, now + 100);
    CHECK(renewed.fresh(now + 159));
    CHECK(renewed.headers.etag == "\"e\"");
}

TEST_CASE("a 304 updates the stored fields it carries, and freshness comes from the result", "[cache]") {
    constexpr std::int64_t now = 784111777; // Sun, 06 Nov 1994 08:49:37 GMT
    auto stored = net::make_cache_entry("https://example.com/a.png", 200, {},
                                        {.cache_control = "max-age=600", .date = "Sun, 06 Nov 1994 08:49:37 GMT",
                                         .etag = "\"e\"", .last_modified = "Sat, 05 Nov 1994 08:49:37 GMT"},
                                        {1, 2, 3}, now);
    REQUIRE(stored);
    net::CacheEntry entry = *stored; // NOLINT(bugprone-unchecked-optional-access): REQUIREd above
    CHECK_FALSE(entry.fresh(now + 600));

    // A 304 an hour later with only a Date: max-age stays, and counts from the new Date.
    net::refresh_cache_entry(entry, {.date = "Sun, 06 Nov 1994 09:49:37 GMT"}, now + 3600);
    CHECK(entry.headers.cache_control == "max-age=600");
    CHECK(entry.headers.etag == "\"e\"");
    CHECK(entry.headers.last_modified == "Sat, 05 Nov 1994 08:49:37 GMT");
    CHECK(entry.fresh(now + 3600 + 599));
    CHECK_FALSE(entry.fresh(now + 3600 + 600));

    // One that changes Cache-Control and the ETag replaces them.
    net::refresh_cache_entry(entry, {.cache_control = "no-cache", .etag = "\"f\""}, now + 4000);
    CHECK(entry.no_cache);
    CHECK_FALSE(entry.fresh(now + 4000));
    CHECK(entry.headers.etag == "\"f\"");
    CHECK(entry.body == std::vector<std::uint8_t>{1, 2, 3});

    CHECK(net::forbids_storing({.cache_control = "max-age=60, no-store"}));
    CHECK_FALSE(net::forbids_storing({.cache_control = "no-cache"}));
    // A `*` anywhere in Vary, also in a list or a second field (joined with ", ").
    CHECK(net::forbids_storing({.vary = "*"}));
    CHECK(net::forbids_storing({.vary = "Accept-Encoding, *"}));
    CHECK(net::forbids_storing({.vary = "Accept,* "}));
    CHECK_FALSE(net::forbids_storing({.vary = "Accept, Accept-Encoding"}));
    CHECK_FALSE(net::make_cache_entry("https://example.com/a.png", 200, {},
                                      {.cache_control = "max-age=60", .vary = "Accept-Encoding, *"}, {1}, now));
    CHECK_FALSE(net::make_cache_entry("https://example.com/b", 301, "/c", {.vary = "Origin, *"}, {}, now));
    CHECK(net::make_cache_entry("https://example.com/a.png", 200, {},
                                {.cache_control = "max-age=60", .vary = "Accept-Encoding"}, {1}, now));
}

TEST_CASE("a 304 without Age drops the stored one, and one without Date dates it on arrival", "[cache]") {
    constexpr std::int64_t now = 784111777; // Sun, 06 Nov 1994 08:49:37 GMT
    const std::string url = "https://example.com/a.png";
    CHECK(net::format_http_date(now) == "Sun, 06 Nov 1994 08:49:37 GMT");
    const std::int64_t later = now + std::int64_t{400} * 86400 + 5;
    CHECK(net::parse_http_date(net::format_http_date(later)) == later);

    // Stored 3000 seconds old with a lifetime of 600: stale at once.
    auto aged = net::make_cache_entry(url, 200, {}, {.cache_control = "max-age=600", .age = "3000", .etag = "\"a\""},
                                      {1}, now);
    REQUIRE(aged);
    // NOLINTBEGIN(bugprone-unchecked-optional-access): both entries are REQUIREd before they're read
    CHECK_FALSE(aged->fresh(now));
    net::refresh_cache_entry(*aged, {.etag = "\"a\""}, now + 10);
    CHECK(aged->headers.age.empty());
    CHECK(aged->fresh(now + 10 + 599));
    CHECK_FALSE(aged->fresh(now + 10 + 600));
    // A 304 with an Age replaces it.
    net::refresh_cache_entry(*aged, {.age = "100"}, now + 20);
    CHECK(aged->headers.age == "100");
    CHECK(aged->fresh(now + 20 + 499));
    CHECK_FALSE(aged->fresh(now + 20 + 500));

    // Stored with a Date; the 304 an hour later has none, so the stored one becomes its arrival.
    auto dated = net::make_cache_entry(url, 200, {},
                                       {.cache_control = "max-age=600", .date = "Sun, 06 Nov 1994 08:49:37 GMT",
                                        .etag = "\"d\""},
                                       {1}, now);
    REQUIRE(dated);
    net::refresh_cache_entry(*dated, {}, now + 3600);
    CHECK(dated->headers.date == "Sun, 06 Nov 1994 09:49:37 GMT");
    CHECK(dated->fresh(now + 3600 + 599));
    CHECK_FALSE(dated->fresh(now + 3600 + 600));
    // A 304 with a Date replaces it.
    net::refresh_cache_entry(*dated, {.date = "Sun, 06 Nov 1994 09:59:37 GMT"}, now + 4200);
    CHECK(dated->headers.date == "Sun, 06 Nov 1994 09:59:37 GMT");
    // NOLINTEND(bugprone-unchecked-optional-access)
}

TEST_CASE("redirects make cache entries with a lifetime of their own, or permanent ones", "[cache]") {
    constexpr std::int64_t now = 784111777;
    const auto redirect = [&](unsigned status, const net::CacheHeaders& headers) {
        return net::make_cache_entry("https://example.com/latest", status, "/v2.png", headers, {}, now);
    };
    // NOLINTBEGIN(bugprone-unchecked-optional-access): each entry is REQUIREd before it's read
    const auto timed = redirect(302, {.cache_control = "max-age=60"});
    REQUIRE(timed);
    CHECK(timed->redirect());
    CHECK(timed->location == "/v2.png");
    CHECK(timed->fresh(now + 59));
    CHECK_FALSE(timed->fresh(now + 60));
    const auto until = redirect(307, {.expires = "Sun, 06 Nov 1994 08:59:37 GMT", .date = "Sun, 06 Nov 1994 08:49:37 GMT"});
    REQUIRE(until);
    CHECK(until->lifetime == 600);
    const auto stale = redirect(303, {.cache_control = "max-age=0"}); // kept: offline, it's still the way
    REQUIRE(stale);
    CHECK_FALSE(stale->fresh(now));
    // Temporary ones without max-age or Expires aren't kept, not even with a Last-Modified.
    CHECK_FALSE(redirect(302, {}));
    CHECK_FALSE(redirect(307, {.date = "Sun, 06 Nov 1994 08:49:37 GMT", .last_modified = "Sat, 05 Nov 1994 08:49:37 GMT"}));
    // Permanent ones are fresh until they're evicted...
    for (const unsigned status : {301u, 308u}) {
        const auto permanent = redirect(status, {});
        REQUIRE(permanent);
        CHECK(permanent->permanent);
        CHECK(permanent->fresh(now + std::int64_t{100} * 365 * 24 * 3600));
    }
    // ...unless they say otherwise.
    const auto short_lived = redirect(301, {.cache_control = "max-age=10"});
    REQUIRE(short_lived);
    CHECK_FALSE(short_lived->fresh(now + 10));
    const auto revalidated = redirect(308, {.cache_control = "no-cache"});
    REQUIRE(revalidated);
    CHECK_FALSE(revalidated->fresh(now));
    // NOLINTEND(bugprone-unchecked-optional-access)
    CHECK_FALSE(redirect(301, {.cache_control = "no-store"}));
    CHECK_FALSE(redirect(308, {.vary = "*"}));
    CHECK_FALSE(net::make_cache_entry("https://example.com/latest", 301, "", {}, {}, now)); // nowhere to go
    CHECK_FALSE(net::make_cache_entry("https://example.com/a", 404, "", {.cache_control = "max-age=60"}, {}, now));
}

TEST_CASE("cache files keep a response's fields and status, and older files are a miss", "[cache]") {
    const TemporaryDirectory directory;
    net::HttpCache cache(directory.path, std::uint64_t{1} << 20);
    const std::int64_t now = net::unix_now();
    auto redirect = net::make_cache_entry("https://example.com/latest", 308, "https://example.com/v2.png",
                                          {.cache_control = "public", .etag = "\"r\""}, {}, now);
    auto image = net::make_cache_entry("https://example.com/v2.png", 200, {},
                                       {.cache_control = "max-age=600", .age = "5", .etag = "\"i\"", .vary = "Accept"},
                                       {4, 5, 6}, now);
    REQUIRE((redirect && image));
    // NOLINTBEGIN(bugprone-unchecked-optional-access): REQUIREd above
    cache.store(*redirect, cache.generation());
    cache.store(*image, cache.generation());
    // NOLINTEND(bugprone-unchecked-optional-access)
    const auto found_redirect = cache.find("https://example.com/latest");
    REQUIRE(found_redirect);
    // NOLINTBEGIN(bugprone-unchecked-optional-access): both are REQUIREd before they're read
    CHECK(found_redirect->status == 308);
    CHECK(found_redirect->location == "https://example.com/v2.png");
    CHECK(found_redirect->permanent);
    CHECK(found_redirect->headers.etag == "\"r\"");
    const auto found_image = cache.find("https://example.com/v2.png");
    REQUIRE(found_image);
    CHECK(found_image->status == 200);
    CHECK(found_image->headers.cache_control == "max-age=600");
    CHECK(found_image->headers.vary == "Accept");
    CHECK(found_image->initial_age == 5);
    CHECK(found_image->fresh(now + 594));
    CHECK_FALSE(found_image->fresh(now + 595));
    CHECK(found_image->body == std::vector<std::uint8_t>{4, 5, 6});
    // NOLINTEND(bugprone-unchecked-optional-access)

    // A file of the previous format, as it would have been written: a miss, and removed.
    std::filesystem::path stored;
    for (const auto& item : std::filesystem::recursive_directory_iterator(directory.path)) {
        std::ifstream in(item.path(), std::ios::binary);
        std::string magic;
        std::string url_line;
        if (item.is_regular_file() && std::getline(in, magic) && std::getline(in, url_line) &&
            url_line == "url https://example.com/v2.png") {
            stored = item.path();
        }
    }
    REQUIRE_FALSE(stored.empty());
    std::ofstream(stored, std::ios::binary | std::ios::trunc)
        << "ELGCACHE 2\nurl https://example.com/v2.png\nredirected 0\nreceived " << now
        << "\ninitial-age 0\nlifetime 600\nno-cache 0\nmust-revalidate 0\netag \nlast-modified \nsize 1\n\nx";
    CHECK_FALSE(cache.find("https://example.com/v2.png"));
    CHECK_FALSE(std::filesystem::exists(stored));

    cache.remove("https://example.com/latest");
    CHECK_FALSE(cache.find("https://example.com/latest"));
    CHECK(directory.files() == 0);
}

TEST_CASE("the disk cache answers while fresh and revalidates after", "[fetch][cache]") {
    const TestServer server;
    const TemporaryDirectory directory;
    const auto cache = std::make_shared<net::HttpCache>(directory.path, 1 << 20);
    const net::FetchOptions options{.cache = cache};

    CHECK(text(fetch(server.url("/cached"), options)) == "cached");
    REQUIRE(wait_until_cached(*cache, server.url("/cached")));
    CHECK(text(fetch(server.url("/cached"), options)) == "cached");
    CHECK(server.hits("/cached") == 1);

    CHECK(text(fetch(server.url("/revalidate"), options)) == "fresh");
    REQUIRE(wait_until_cached(*cache, server.url("/revalidate")));
    CHECK(text(fetch(server.url("/revalidate"), options)) == "fresh"); // the stored body, after a 304
    CHECK(server.hits("/revalidate") == 2);
    CHECK(server.not_modified() == 1);

    // A 304 without Cache-Control keeps the stored max-age; without Age, the stored Age goes, so
    // the entry is fresh again.
    CHECK(text(fetch(server.url("/aged"), options)) == "aged"); // stored, but stale at once
    REQUIRE(wait_until_cached(*cache, server.url("/aged")));
    CHECK(text(fetch(server.url("/aged"), options)) == "aged"); // revalidated: a 304
    CHECK(server.hits("/aged") == 2);
    const auto fresh_in_cache = [&] {
        const std::optional<net::CacheEntry> entry = cache->find(server.url("/aged"));
        return entry && entry->fresh(net::unix_now());
    };
    for (int i = 0; i < 200 && !fresh_in_cache(); ++i) {
        std::this_thread::sleep_for(10ms); // until the update is stored
    }
    CHECK(text(fetch(server.url("/aged"), options)) == "aged");
    CHECK(server.hits("/aged") == 2); // fresh now: from the cache

    // no-store in a 304: answered with the stored body, which is then removed.
    CHECK(text(fetch(server.url("/gone-on-304"), options)) == "kept");
    REQUIRE(wait_until_cached(*cache, server.url("/gone-on-304")));
    CHECK(text(fetch(server.url("/gone-on-304"), options)) == "kept");
    for (int i = 0; i < 200 && cache->find(server.url("/gone-on-304")); ++i) {
        std::this_thread::sleep_for(10ms);
    }
    CHECK_FALSE(cache->find(server.url("/gone-on-304")));
    CHECK(server.hits("/gone-on-304") == 2);

    const std::size_t before = directory.files();
    CHECK(text(fetch(server.url("/nostore"), options)) == "secret");
    CHECK(text(fetch(server.url("/nostore"), options)) == "secret");
    CHECK(server.hits("/nostore") == 2);
    CHECK(directory.files() == before);

    // no-store in a second Cache-Control field counts too. (Stored entries appear within a few
    // milliseconds; this one must not.)
    CHECK(text(fetch(server.url("/two-fields"), options)) == "split");
    for (int i = 0; i < 30 && !cache->find(server.url("/two-fields")); ++i) {
        std::this_thread::sleep_for(10ms);
    }
    CHECK_FALSE(cache->find(server.url("/two-fields")));
}

TEST_CASE("a response reached through a redirect is stored under its own URL", "[fetch][cache]") {
    const TestServer server;
    const TemporaryDirectory directory;
    const auto cache = std::make_shared<net::HttpCache>(directory.path, 1 << 20);
    const net::FetchOptions options{.cache = cache};
    CHECK(text(fetch(server.url("/latest"), options)) == "v123");
    REQUIRE(wait_until_cached(*cache, server.url("/v123.png")));
    const std::optional<net::CacheEntry> image = cache->find(server.url("/v123.png"));
    REQUIRE(image);
    CHECK(image->lifetime == 31536000); // NOLINT(bugprone-unchecked-optional-access): REQUIREd above
    CHECK_FALSE(cache->find(server.url("/latest"))); // neither the image nor the redirect, which has no lifetime
    // So /latest is asked again, and leads to the stored image.
    CHECK(text(fetch(server.url("/latest"), options)) == "v123");
    CHECK(server.hits("/latest") == 2);
    CHECK(server.hits("/v123.png") == 1);
}

TEST_CASE("redirects are stored under their own URL and followed from the cache while fresh", "[fetch][cache]") {
    const TestServer server;
    const TemporaryDirectory directory;
    const auto cache = std::make_shared<net::HttpCache>(directory.path, 1 << 20);
    const net::FetchOptions options{.cache = cache};

    // A temporary redirect with a lifetime.
    CHECK(text(fetch(server.url("/moved"), options)) == "hello");
    REQUIRE(wait_until_cached(*cache, server.url("/moved")));
    const std::optional<net::CacheEntry> moved = cache->find(server.url("/moved"));
    REQUIRE(moved);
    CHECK(moved->status == 302); // NOLINT(bugprone-unchecked-optional-access): REQUIREd above
    CHECK(text(fetch(server.url("/moved"), options)) == "hello");
    CHECK(server.hits("/moved") == 1);
    CHECK(server.hits("/hello") == 2); // not cacheable itself

    // A permanent one without: fresh until it's evicted.
    CHECK(text(fetch(server.url("/permanent"), options)) == "hello");
    REQUIRE(wait_until_cached(*cache, server.url("/permanent")));
    CHECK(text(fetch(server.url("/permanent"), options)) == "hello");
    CHECK(server.hits("/permanent") == 1);

    // A stale one goes to the server again.
    CHECK(text(fetch(server.url("/stale-redirect"), options)) == "stale");
    REQUIRE(wait_until_cached(*cache, server.url("/stale-redirect")));
    CHECK(text(fetch(server.url("/stale-redirect"), options)) == "stale");
    CHECK(server.hits("/stale-redirect") == 2);

    // Redirects from the cache count toward the limit too.
    CHECK(fetch(server.url("/moved"), {.max_redirects = 0, .cache = cache}).error() == "too many redirects (more than 0)");
}

TEST_CASE("a stale stored redirect is followed when the server is gone", "[fetch][cache]") {
    const TemporaryDirectory directory;
    const auto cache = std::make_shared<net::HttpCache>(directory.path, 1 << 20);
    std::string url;
    {
        const TestServer server;
        url = server.url("/stale-redirect");
        CHECK(text(fetch(url, {.cache = cache})) == "stale");
        REQUIRE(wait_until_cached(*cache, url));
        REQUIRE(wait_until_cached(*cache, server.url("/stale")));
    }
    // The stale redirect, then the stale copy it leads to.
    CHECK(text(fetch(url, {.cache = cache})) == "stale");
    CHECK_FALSE(fetch(url).has_value());
}

TEST_CASE("the disk cache answers with a stale copy when the server is gone", "[fetch][cache]") {
    const TemporaryDirectory directory;
    const auto cache = std::make_shared<net::HttpCache>(directory.path, 1 << 20);
    std::string url;
    {
        const TestServer server;
        url = server.url("/stale");
        CHECK(text(fetch(url, {.cache = cache})) == "stale");
        REQUIRE(wait_until_cached(*cache, url));
    }
    CHECK(text(fetch(url, {.cache = cache})) == "stale");
    CHECK_FALSE(fetch(url).has_value());
}

TEST_CASE("the disk cache keeps to its limit and clears only its own files", "[cache]") {
    const TemporaryDirectory directory;
    std::filesystem::create_directories(directory.path / "ab");
    std::ofstream(directory.path / "ab" / "notes.txt") << "not the cache's";
    std::ofstream(directory.path / "keep.txt") << "nor this";

    net::HttpCache cache(directory.path, std::uint64_t{16} * 1024);
    const auto store = [&](int i) {
        auto entry = net::make_cache_entry(std::format("https://example.com/{}.png", i), 200, {},
                                           {.cache_control = "max-age=600"}, std::vector<std::uint8_t>(1500, 7),
                                           net::unix_now());
        REQUIRE(entry);
        cache.store(*entry, cache.generation());
        std::this_thread::sleep_for(2ms); // distinct times of use
    };
    for (int i = 0; i < 12; ++i) {
        store(i);
    }
    CHECK_FALSE(cache.find("https://example.com/0.png"));
    CHECK(cache.find("https://example.com/11.png"));

    // Stores begun before a clear are dropped.
    const std::uint64_t before = cache.generation();
    cache.clear();
    auto late = net::make_cache_entry("https://example.com/late.png", 200, {}, {.cache_control = "max-age=600"}, {1},
                                      net::unix_now());
    REQUIRE(late);
    cache.store(*late, before); // NOLINT(bugprone-unchecked-optional-access): REQUIREd above
    CHECK_FALSE(cache.find("https://example.com/late.png"));
    CHECK_FALSE(cache.find("https://example.com/11.png"));
    CHECK(std::filesystem::exists(directory.path / "ab" / "notes.txt"));
    CHECK(std::filesystem::exists(directory.path / "keep.txt"));

    // A retired cache (replaced by set_image_cache) stores nothing.
    cache.retire();
    cache.store(*late, cache.generation()); // NOLINT(bugprone-unchecked-optional-access): REQUIREd above
    CHECK_FALSE(cache.find("https://example.com/late.png"));
}

TEST_CASE("values that would break a cache file's lines aren't written to it", "[cache]") {
    const std::int64_t now = net::unix_now();
    const auto entry = [&](std::string etag) {
        return net::make_cache_entry("https://example.com/a.png", 200, {},
                                     {.cache_control = "max-age=600", .etag = std::move(etag)}, {1}, now);
    };
    CHECK_FALSE(entry("\"e\"\r\nlifetime 999999999"));
    CHECK_FALSE(entry(std::string("\"e\0\"", 4)));
    auto kept = entry("\"e\"");
    REQUIRE(kept);
    // NOLINTBEGIN(bugprone-unchecked-optional-access): REQUIREd above
    net::refresh_cache_entry(*kept, {.cache_control = "max-age=600", .etag = "\"f\"\nlifetime 9"}, now);
    CHECK(kept->headers.etag == "\"e\""); // the 304's can't be written: the old one stays
    // NOLINTEND(bugprone-unchecked-optional-access)
}

TEST_CASE("a damaged or truncated cache file is a miss, and goes", "[cache]") {
    const TemporaryDirectory directory;
    const std::string url = "https://example.com/a.png";
    const auto file = [&] {
        for (const auto& item : std::filesystem::recursive_directory_iterator(directory.path)) {
            if (item.is_regular_file()) {
                return item.path();
            }
        }
        return std::filesystem::path();
    };
    for (const bool truncate : {true, false}) {
        net::HttpCache cache(directory.path, std::uint64_t{1} << 20);
        auto entry = net::make_cache_entry(url, 200, {}, {.cache_control = "max-age=600"}, {1, 2, 3, 4}, net::unix_now());
        REQUIRE(entry);
        cache.store(*entry, cache.generation()); // NOLINT(bugprone-unchecked-optional-access): REQUIREd above
        const std::filesystem::path stored = file();
        REQUIRE_FALSE(stored.empty());
        if (truncate) {
            std::filesystem::resize_file(stored, std::filesystem::file_size(stored) - 1); // a body byte short
        } else {
            std::ofstream(stored, std::ios::binary | std::ios::trunc) << "not a cache file";
        }
        CHECK_FALSE(cache.find(url));
        CHECK_FALSE(std::filesystem::exists(stored));
    }
}

TEST_CASE("AsyncImage fails on an image with too many pixels, and logs why", "[fetch][image]") {
    const TestServer server;
    const AsyncImagePhase phase = load_async_image(server.url("/huge.png"));
    CHECK(phase.kind == AsyncImagePhase::Kind::failure);
    CHECK_THAT(phase.error, ContainsSubstring("more than the 134217728"));
    CHECK(logged(std::format("AsyncImage: {}: {}", server.url("/huge.png"), phase.error)));
}

TEST_CASE("an AsyncImage's limits can be lowered or raised for it", "[fetch][image]") {
    const TestServer server;
    const AsyncImagePhase lowered = load_async_image(server.url("/small.png"), {.disk_cache = false, .max_pixels = 2047});
    CHECK(lowered.kind == AsyncImagePhase::Kind::failure);
    CHECK_THAT(lowered.error, ContainsSubstring("64 x 32 pixels, more than the 2047"));
    CHECK(load_async_image(server.url("/small.png")).kind == AsyncImagePhase::Kind::success);
    // Raised: no longer a failure (it isn't decoded until its image is shown).
    const AsyncImagePhase raised =
        load_async_image(server.url("/huge.png"), {.disk_cache = false, .max_pixels = std::uint64_t{1} << 28});
    CHECK(raised.kind == AsyncImagePhase::Kind::success);
}

TEST_CASE("an AsyncImage applies its own limits to an image another view downloaded", "[fetch][image]") {
    const TestServer server;
    const std::string url = server.url("/huge.png");
    // The first view's raised limit lets it in, and into the memory cache...
    CHECK(load_async_image(url, {.disk_cache = false, .max_pixels = std::uint64_t{1} << 28}).kind ==
          AsyncImagePhase::Kind::success);
    // ...where the next view, with the default limit, finds it too large.
    const AsyncImagePhase next = load_async_image(url);
    CHECK(next.kind == AsyncImagePhase::Kind::failure);
    CHECK_THAT(next.error, ContainsSubstring("more than the 134217728"));
    CHECK(server.hits("/huge.png") == 1);
}

TEST_CASE("AsyncImage logs the limit a download exceeded", "[fetch][image]") {
    const TestServer server;
    const AsyncImagePhase phase = load_async_image(server.url("/header-70k"));
    CHECK(phase.kind == AsyncImagePhase::Kind::failure);
    CHECK(phase.error == "the response's header section is larger than 64 KiB");
    CHECK(logged(std::format("AsyncImage: {}: {}", server.url("/header-70k"), phase.error)));
}

TEST_CASE("AsyncImage builds", "[fetch][image]") {
    CHECK_NOTHROW(AsyncImage("https://example.com/a.png"));
    CHECK_NOTHROW(AsyncImage("https://example.com/a.png", {.scale = 2, .content_mode = ContentMode::fill}).frame(40, 40));
    CHECK_NOTHROW(AsyncImage("https://example.com/a.png", [](const AsyncImagePhase& phase) {
        return phase.kind == AsyncImagePhase::Kind::success ? phase.image.resizable() : Text("…");
    }));
    CHECK_NOTHROW(AsyncImage("https://example.com/a.png", [](const AsyncImagePhase&) { return Text("…"); },
                             {.scale = 2, .disk_cache = false}));
    CHECK_NOTHROW(set_image_cache({.disk = false}));
    CHECK_NOTHROW(clear_image_cache());
    CHECK_NOTHROW(set_image_cache({}));
    CHECK_THROWS_AS(AsyncImage("https://example.com/a.png", std::function<View(const AsyncImagePhase&)>()), UsageError);
}
