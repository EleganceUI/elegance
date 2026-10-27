// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// AsyncImage's downloads (net/fetch.h) against a local server: plain HTTP, and HTTPS with a test
// certificate authority, so certificate and host name checks are exercised without the network.

#include "net/fetch.h"

#include <elg/elegance.h>

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>

#include <catch2/catch_test_macros.hpp>

#include <future>
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
class TestServer {
public:
    explicit TestServer(bool secure = false) : secure_(secure) {
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

private:
    template <class Stream>
    awaitable<void> serve(Stream& stream) {
        boost::beast::flat_buffer buffer;
        bhttp::request<bhttp::string_body> request;
        co_await bhttp::async_read(stream, buffer, request, use_awaitable);
        bhttp::response<bhttp::string_body> response{bhttp::status::ok, 11};
        const std::string target(request.target());
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
            asio::steady_timer timer(co_await asio::this_coro::executor, 60s);
            co_await timer.async_wait(use_awaitable);
        } else {
            response.result(bhttp::status::not_found);
        }
        response.prepare_payload();
        co_await bhttp::async_write(stream, response, use_awaitable);
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
            asio::co_spawn(io_, session(std::move(socket)), asio::detached);
        }
    }

    bool secure_;
    asio::io_context io_;
    asio::ip::tcp::acceptor acceptor_{io_, {asio::ip::make_address("127.0.0.1"), 0}};
    ssl::context tls_{ssl::context::tls_server};
    std::thread thread_;
};

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

TEST_CASE("AsyncImage builds", "[fetch][image]") {
    CHECK_NOTHROW(AsyncImage("https://example.com/a.png"));
    CHECK_NOTHROW(AsyncImage("https://example.com/a.png", {.scale = 2, .content_mode = ContentMode::fill}).frame(40, 40));
    CHECK_NOTHROW(AsyncImage("https://example.com/a.png", [](const AsyncImagePhase& phase) {
        return phase.kind == AsyncImagePhase::Kind::success ? phase.image.resizable() : Text("…");
    }));
    CHECK_THROWS_AS(AsyncImage("https://example.com/a.png", std::function<View(const AsyncImagePhase&)>()), UsageError);
}
