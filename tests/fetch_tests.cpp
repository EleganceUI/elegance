// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// AsyncImage's downloads (net/fetch.h) against a local server: plain HTTP, and HTTPS with a test
// certificate authority, so certificate and host name checks are exercised without the network.

#include "net/fetch.h"
#include "net/http_cache.h"
#include "net/proxy.h"

#include <elg/elegance.h>

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <array>
#include <filesystem>
#include <fstream>
#include <future>
#include <map>
#include <mutex>
#include <random>
#include <string>
#include <thread>

using namespace elg;
using namespace std::chrono_literals;

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
    template <class Stream>
    awaitable<void> serve(Stream& stream) {
        boost::beast::flat_buffer buffer;
        while (true) {
            bhttp::request<bhttp::string_body> request;
            co_await bhttp::async_read(stream, buffer, request, use_awaitable);
            auto response = respond(request);
            co_await serve_slowly(request);
            co_await bhttp::async_write(stream, response, use_awaitable);
            if (drop_ || !response.keep_alive()) {
                co_return;
            }
        }
    }

    awaitable<void> serve_slowly(const bhttp::request<bhttp::string_body>& request) {
        if (request.target() == "/slow") {
            asio::steady_timer timer(co_await asio::this_coro::executor, 60s);
            co_await timer.async_wait(use_awaitable);
        }
    }

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
            // The client gave up (a refused certificate, a cancelled request).
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

    ~TestProxy() {
        io_.stop();
        thread_.join();
    }

    std::string url(std::string_view credentials = {}) const {
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
            to->shutdown(asio::ip::tcp::socket::shutdown_send, ignored);
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
                co_await bhttp::async_write(*client, refusal, use_awaitable);
                co_return;
            }
            if (request.method() != bhttp::verb::connect) {
                bhttp::response<bhttp::string_body> response{bhttp::status::ok, 11};
                response.body() = "via proxy " + std::string(request.target());
                response.keep_alive(false);
                response.prepare_payload();
                co_await bhttp::async_write(*client, response, use_awaitable);
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
    ~TemporaryDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
    // Stored entries (not the temporary files of stores under way).
    std::size_t files() const {
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

std::string text(const net::FetchResult& result) {
    REQUIRE(result.has_value());
    return {result->begin(), result->end()};
}

} // namespace

TEST_CASE("downloads follow redirects and report HTTP errors", "[fetch]") {
    const TestServer server;
    CHECK(text(fetch(server.url("/hello"))) == "hello");
    CHECK(text(fetch(server.url("/redirect"))) == "hello");
    CHECK(fetch(server.url("/missing")).error() == "HTTP 404");
    CHECK(fetch(server.url("/loop")).error() == "too many redirects");
    CHECK(fetch(server.url("/big"), {.max_body = 1024}).error() == "the response is too large");
    CHECK(text(fetch(server.url("/big"))).size() == 4096);
}

TEST_CASE("downloads refuse what isn't an http(s) URL", "[fetch]") {
    CHECK(fetch("file:///etc/hosts").error() == "not an http(s) URL");
    CHECK(fetch("not a url").error() == "not an http(s) URL");
    CHECK(fetch("https://").error() == "not an http(s) URL");
}

TEST_CASE("downloads time out", "[fetch]") {
    const TestServer server;
    CHECK(fetch(server.url("/slow"), {.timeout = 200ms}).error() == "the request timed out");
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
    CHECK((*full)->host == "proxy.corp");
    CHECK((*full)->port == "8080");
    CHECK((*full)->authorization == "Basic dXMgZXI6cEBzcw=="); // "us er:p@ss"
    const net::ProxyChoice bare = net::parse_proxy_url("proxy.corp");
    REQUIRE((bare && bare->has_value()));
    CHECK((*bare)->port == "1080");
    CHECK((*bare)->authorization.empty());
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
    CHECK(https->value()->host == "secure.corp");
    CHECK_FALSE(net::proxy_from_environment("http", "example.com", "80", get));
    environment["http_proxy"] = "plain.corp:8080";
    const auto http = net::proxy_from_environment("http", "example.com", "80", get);
    REQUIRE((http && *http && http->value()));
    CHECK(http->value()->host == "plain.corp");
    environment["NO_PROXY"] = "example.com";
    const auto bypassed = net::proxy_from_environment("https", "example.com", "443", get);
    REQUIRE((bypassed && *bypassed));
    CHECK_FALSE(bypassed->value().has_value());

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

TEST_CASE("HTTP dates and freshness", "[cache]") {
    constexpr std::int64_t kNov6 = 784111777; // Sun, 06 Nov 1994 08:49:37 GMT
    CHECK(net::parse_http_date("Sun, 06 Nov 1994 08:49:37 GMT") == kNov6);
    CHECK(net::parse_http_date("Sunday, 06-Nov-94 08:49:37 GMT") == kNov6);
    CHECK(net::parse_http_date("Sun Nov  6 08:49:37 1994") == kNov6);
    CHECK_FALSE(net::parse_http_date("0"));
    CHECK_FALSE(net::parse_http_date("Sun, 31 Feb 1994 08:49:37 GMT"));

    const std::int64_t now = kNov6;
    const auto entry = [&](net::CacheHeaders headers) {
        return net::make_cache_entry("https://example.com/a.png", false, headers, {1, 2, 3}, now);
    };
    const auto max_age = entry({.cache_control = "public, max-age=60"});
    REQUIRE(max_age);
    CHECK(max_age->fresh(now + 59));
    CHECK_FALSE(max_age->fresh(now + 60));
    // Age counts against it; so does a Date in the past.
    CHECK_FALSE(entry({.cache_control = "max-age=60", .age = "60"}));
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
    net::refresh_cache_entry(renewed, {.cache_control = "max-age=60"}, now + 100);
    CHECK(renewed.fresh(now + 159));
    CHECK(renewed.etag == "\"e\"");
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

    const std::size_t before = directory.files();
    CHECK(text(fetch(server.url("/nostore"), options)) == "secret");
    CHECK(text(fetch(server.url("/nostore"), options)) == "secret");
    CHECK(server.hits("/nostore") == 2);
    CHECK(directory.files() == before);
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

    net::HttpCache cache(directory.path, 16 * 1024);
    const auto store = [&](int i) {
        auto entry = net::make_cache_entry(std::format("https://example.com/{}.png", i), false,
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
    auto late = net::make_cache_entry("https://example.com/late.png", false, {.cache_control = "max-age=600"}, {1},
                                      net::unix_now());
    REQUIRE(late);
    cache.store(*late, before);
    CHECK_FALSE(cache.find("https://example.com/late.png"));
    CHECK_FALSE(cache.find("https://example.com/11.png"));
    CHECK(std::filesystem::exists(directory.path / "ab" / "notes.txt"));
    CHECK(std::filesystem::exists(directory.path / "keep.txt"));

    // A retired cache (replaced by set_image_cache) stores nothing.
    cache.retire();
    cache.store(*late, cache.generation());
    CHECK_FALSE(cache.find("https://example.com/late.png"));
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
