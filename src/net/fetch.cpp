// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Downloads on Boost.Beast and Asio (design.md §13.3): one network thread runs every request as a
// coroutine. TLS through OpenSSL, trusting the system's root certificates.

#include "net/fetch.h"

#include "core/log.h"

#if defined(_WIN32)
#include <windows.h>
#include <wincrypt.h>
#endif

#include <boost/asio.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/url.hpp>

#if defined(__APPLE__)
#include <Security/Security.h>
#endif

#include <atomic>
#include <filesystem>
#include <format>
#include <thread>
#include <variant>

namespace elg::detail::net {

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace bhttp = boost::beast::http;
namespace ssl = boost::asio::ssl;
namespace urls = boost::urls;
using asio::awaitable;
using asio::use_awaitable;

struct FetchTask::State {
    std::atomic<bool> cancelled{false};
    asio::cancellation_signal signal; // network thread only
};

namespace {

// The thread every request runs on.
class Network {
public:
    static Network& instance() {
        static Network network;
        return network;
    }

    asio::io_context& io() { return io_; }

    ~Network() {
        work_.reset();
        io_.stop();
        thread_.join();
    }

private:
    Network() : work_(asio::make_work_guard(io_)), thread_([this] { io_.run(); }) {}

    asio::io_context io_;
    asio::executor_work_guard<asio::io_context::executor_type> work_;
    std::thread thread_;
};

// The system's root certificates: the certificate store on Windows, the keychain's anchors on
// macOS, the distribution's bundle elsewhere (OpenSSL's own default path is vcpkg's, not the
// system's). Returns how many it added.
int add_system_roots(ssl::context& context) {
    X509_STORE* store = SSL_CTX_get_cert_store(context.native_handle());
    int added = 0;
    const auto add_der = [&](const unsigned char* data, long size) {
        if (X509* certificate = d2i_X509(nullptr, &data, size)) {
            added += X509_STORE_add_cert(store, certificate) == 1;
            X509_free(certificate);
        }
    };
#if defined(_WIN32)
    if (HCERTSTORE system = CertOpenSystemStoreW(0, L"ROOT")) {
        for (PCCERT_CONTEXT certificate = nullptr; (certificate = CertEnumCertificatesInStore(system, certificate));) {
            add_der(certificate->pbCertEncoded, static_cast<long>(certificate->cbCertEncoded));
        }
        CertCloseStore(system, 0);
    }
#elif defined(__APPLE__)
    CFArrayRef anchors = nullptr;
    if (SecTrustCopyAnchorCertificates(&anchors) == errSecSuccess && anchors) {
        for (CFIndex i = 0; i < CFArrayGetCount(anchors); ++i) {
            const auto certificate = static_cast<SecCertificateRef>(const_cast<void*>(CFArrayGetValueAtIndex(anchors, i)));
            if (CFDataRef der = SecCertificateCopyData(certificate)) {
                add_der(CFDataGetBytePtr(der), static_cast<long>(CFDataGetLength(der)));
                CFRelease(der);
            }
        }
        CFRelease(anchors);
    }
#else
    boost::system::error_code ignored;
    context.set_default_verify_paths(ignored); // SSL_CERT_FILE and SSL_CERT_DIR, when set
    for (const char* bundle : {"/etc/ssl/certs/ca-certificates.crt", "/etc/pki/tls/certs/ca-bundle.crt",
                               "/etc/ssl/ca-bundle.pem", "/etc/pki/ca-trust/extracted/pem/tls-ca-bundle.pem",
                               "/etc/ssl/cert.pem"}) {
        std::error_code missing;
        if (std::filesystem::exists(bundle, missing)) {
            context.load_verify_file(bundle, ignored);
            if (!ignored) {
                added = sk_X509_OBJECT_num(X509_STORE_get0_objects(store));
                break;
            }
        }
    }
#endif
    return added;
}

std::shared_ptr<ssl::context> make_context(const std::string& roots) {
    auto context = std::make_shared<ssl::context>(ssl::context::tls_client);
    SSL_CTX_set_min_proto_version(context->native_handle(), TLS1_2_VERSION);
    context->set_verify_mode(ssl::verify_peer);
    if (!roots.empty()) {
        context->add_certificate_authority(asio::buffer(roots));
    } else if (add_system_roots(*context) == 0) {
        log_warning("net: no system root certificates found; https downloads will fail");
    }
    return context;
}

std::shared_ptr<ssl::context> system_context() {
    static const std::shared_ptr<ssl::context> context = make_context({});
    return context;
}

std::string describe(const boost::system::error_code& error) {
    namespace e = asio::error;
    if (error == e::operation_aborted) {
        return "cancelled";
    }
    if (error == e::host_not_found || error == e::host_not_found_try_again || error == e::no_data) {
        return "the server's name couldn't be resolved";
    }
    if (error == e::connection_refused || error == e::network_unreachable || error == e::host_unreachable ||
        error == e::timed_out) {
        return "couldn't connect to the server";
    }
    if (error.category() == e::get_ssl_category() || error.category() == ssl::error::get_stream_category()) {
        return std::format("the secure connection failed: {}", error.message());
    }
    if (error == bhttp::error::body_limit) {
        return "the response is too large";
    }
    if (error == e::eof || error == e::connection_reset || error == bhttp::error::end_of_stream ||
        error == bhttp::error::partial_message) {
        return "the connection was closed early";
    }
    if (error.category() == make_error_code(bhttp::error::bad_version).category()) { // Beast's parser
        return "the server's response isn't valid HTTP";
    }
    return error.message();
}

struct Reply {
    unsigned status = 0;
    std::string location;
    std::vector<std::uint8_t> body;
};

template <class Stream>
awaitable<Reply> exchange(Stream& stream, std::string host, std::string target, std::size_t max_body) {
    bhttp::request<bhttp::empty_body> request{bhttp::verb::get, target, 11};
    request.set(bhttp::field::host, host);
    request.set(bhttp::field::user_agent, "Elegance");
    request.set(bhttp::field::accept, "*/*");
    request.set(bhttp::field::accept_encoding, "identity");
    request.set(bhttp::field::connection, "close");
    co_await bhttp::async_write(stream, request, use_awaitable);

    beast::flat_buffer buffer;
    bhttp::response_parser<bhttp::vector_body<std::uint8_t>> parser;
    parser.header_limit(64 * 1024);
    parser.body_limit(max_body);
    co_await bhttp::async_read(stream, buffer, parser, use_awaitable);
    auto& response = parser.get();
    co_return Reply{response.result_int(), std::string(response[bhttp::field::location]), std::move(response.body())};
}

// One request, no redirects. Throws boost::system::system_error.
awaitable<Reply> request(urls::url url, std::shared_ptr<ssl::context> tls, std::size_t max_body) {
    const auto executor = co_await asio::this_coro::executor;
    const bool secure = url.scheme_id() == urls::scheme::https;
    const std::string host = url.host_address();
    const std::string port = url.has_port() && !url.port().empty() ? std::string(url.port()) : secure ? "443" : "80";
    std::string target = url.encoded_path().empty() ? "/" : std::string(url.encoded_path());
    if (url.has_query()) {
        target += '?';
        target += url.encoded_query();
    }
    const std::string host_header(url.encoded_host_and_port());

    asio::ip::tcp::resolver resolver(executor);
    const auto endpoints = co_await resolver.async_resolve(host, port, use_awaitable);
    beast::tcp_stream tcp(executor);
    co_await tcp.async_connect(endpoints, use_awaitable);
    if (!secure) {
        co_return co_await exchange(tcp, host_header, std::move(target), max_body);
    }
    ssl::stream<beast::tcp_stream> stream(std::move(tcp), *tls);
    if (url.host_type() == urls::host_type::name) {
        SSL_set_tlsext_host_name(stream.native_handle(), host.c_str()); // SNI
    }
    stream.set_verify_mode(ssl::verify_peer);
    stream.set_verify_callback(ssl::host_name_verification(host));
    co_await stream.async_handshake(ssl::stream_base::client, use_awaitable);
    // The response is length-delimited and the connection closes after it, so no TLS shutdown.
    co_return co_await exchange(stream, host_header, std::move(target), max_body);
}

bool is_redirect(unsigned status) {
    return status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
}

// The request and its redirects. Doesn't throw (the timeout's || would wait out its timer).
awaitable<FetchResult> follow(std::string text, FetchOptions options, std::shared_ptr<ssl::context> tls) {
    try {
        const auto parsed = urls::parse_uri(text);
        if (!parsed) {
            co_return std::unexpected("not an http(s) URL");
        }
        urls::url url = *parsed;
        for (int redirects = 0;; ++redirects) {
            if ((url.scheme_id() != urls::scheme::http && url.scheme_id() != urls::scheme::https) ||
                url.host_address().empty()) {
                co_return std::unexpected("not an http(s) URL");
            }
            Reply reply = co_await request(url, tls, options.max_body);
            if (is_redirect(reply.status) && !reply.location.empty()) {
                if (redirects == options.max_redirects) {
                    co_return std::unexpected("too many redirects");
                }
                const auto reference = urls::parse_uri_reference(reply.location);
                urls::url next;
                if (!reference || !urls::resolve(url, *reference, next)) {
                    co_return std::unexpected("the server redirected to an invalid URL");
                }
                if (url.scheme_id() == urls::scheme::https && next.scheme_id() != urls::scheme::https) {
                    co_return std::unexpected("the server redirected from https to http");
                }
                url = std::move(next);
                continue;
            }
            if (reply.status < 200 || reply.status >= 300) {
                co_return std::unexpected(std::format("HTTP {}", reply.status));
            }
            co_return std::move(reply.body);
        }
    } catch (const boost::system::system_error& failure) {
        co_return std::unexpected(describe(failure.code()));
    }
}

awaitable<FetchResult> run(std::string url, FetchOptions options, std::shared_ptr<ssl::context> tls) {
    using namespace asio::experimental::awaitable_operators;
    asio::steady_timer timer(co_await asio::this_coro::executor, options.timeout);
    auto outcome = co_await (follow(std::move(url), std::move(options), std::move(tls)) ||
                             timer.async_wait(use_awaitable));
    if (outcome.index() == 1) {
        co_return std::unexpected("the request timed out");
    }
    co_return std::move(std::get<0>(outcome));
}

} // namespace

void FetchTask::cancel() const {
    if (!state_ || state_->cancelled.exchange(true)) {
        return;
    }
    asio::post(Network::instance().io(), [state = state_] { state->signal.emit(asio::cancellation_type::terminal); });
}

FetchTask fetch(std::string url, std::function<void(FetchResult)> done, FetchOptions options) {
    auto state = std::make_shared<FetchTask::State>();
    asio::io_context& io = Network::instance().io();
    asio::post(io, [&io, state, url = std::move(url), done = std::move(done), options = std::move(options)]() mutable {
        if (state->cancelled) {
            return;
        }
        std::shared_ptr<ssl::context> tls;
        try {
            tls = options.root_certificates.empty() ? system_context() : make_context(options.root_certificates);
        } catch (const boost::system::system_error& failure) {
            done(std::unexpected(std::format("the secure connection failed: {}", failure.code().message())));
            return;
        }
        asio::co_spawn(io, run(std::move(url), std::move(options), std::move(tls)),
                       asio::bind_cancellation_slot(state->signal.slot(),
                                                    [state, done = std::move(done)](std::exception_ptr failure,
                                                                                    FetchResult result) {
                                                        if (state->cancelled) {
                                                            return;
                                                        }
                                                        if (failure) {
                                                            result = std::unexpected("the download failed");
                                                        }
                                                        done(std::move(result));
                                                    }));
    });
    return FetchTask(state);
}

} // namespace elg::detail::net
