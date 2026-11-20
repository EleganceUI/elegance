// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Downloads on Boost.Beast and Asio (design.md §13.3): one network thread runs every request as a
// coroutine. TLS through OpenSSL, trusting the system's root certificates. Connections are kept
// open for the next request to the same place; proxy lookups and the disk cache run on a couple
// of threads beside it.

#include "net/fetch.h"

#include "core/profile.h"
#include "net/http_cache.h"
#include "net/net_backend.h"
#include "net/proxy.h"

#include "core/log.h"

#include <boost/asio.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/url.hpp>
#include <openssl/crypto.h>
#include <openssl/ssl.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <format>
#include <map>
#include <stdexcept>
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
    // Pending until cancelled or until its callback starts, whichever comes first.
    enum class Stage { pending, cancelled, called };
    std::atomic<Stage> stage{Stage::pending};
    asio::cancellation_signal signal; // network thread only

    // Whether the callback may start now; if so, cancelling comes too late from here on.
    bool start_callback() {
        Stage expected = Stage::pending;
        return stage.compare_exchange_strong(expected, Stage::called);
    }
};

namespace {

// An open connection, plain or TLS, with what was read past the last response.
struct Connection {
    explicit Connection(beast::tcp_stream tcp) : stream(std::in_place_index<0>, std::move(tcp)) {}
    Connection(beast::tcp_stream tcp, ssl::context& tls) : stream(std::in_place_index<1>, std::move(tcp), tls) {}

    std::variant<beast::tcp_stream, ssl::stream<beast::tcp_stream>> stream;
    beast::flat_buffer buffer;
};

// Connections kept open after a response, by where they go (host, port, proxy, trust). Network
// thread only.
class ConnectionPool {
public:
    std::unique_ptr<Connection> take(const std::string& key) {
        const auto now = std::chrono::steady_clock::now();
        std::erase_if(idle_, [&](const auto& item) { return now - item.second.since > kIdleFor; });
        const auto it = idle_.find(key);
        if (it == idle_.end()) {
            return nullptr;
        }
        std::unique_ptr<Connection> connection = std::move(it->second.connection);
        idle_.erase(it);
        return connection;
    }

    void put(std::string key, std::unique_ptr<Connection> connection) {
        if (idle_.count(key) >= kPerHost || idle_.size() >= kTotal) {
            return; // closed
        }
        idle_.emplace(std::move(key), Idle{std::move(connection), std::chrono::steady_clock::now()});
    }

private:
    static constexpr auto kIdleFor = std::chrono::seconds(30);
    static constexpr std::size_t kPerHost = 6;
    static constexpr std::size_t kTotal = 32;
    struct Idle {
        std::unique_ptr<Connection> connection;
        std::chrono::steady_clock::time_point since;
    };
    std::multimap<std::string, Idle> idle_;
};

// The thread every request runs on, and threads for what blocks (proxy lookups, the disk cache).
class Network {
public:
    static Network& instance() {
        static Network network;
        return network;
    }

    asio::io_context& io() { return io_; }
    ConnectionPool& pool() { return pool_; }
    asio::thread_pool& blocking() { return blocking_; }

    Network(const Network&) = delete;
    Network(Network&&) = delete;
    Network& operator=(const Network&) = delete;
    Network& operator=(Network&&) = delete;
    ~Network() {
        work_.reset();
        io_.stop();
        thread_.join();
        blocking_.join();
    }

private:
    Network() : work_(asio::make_work_guard(io_)), thread_([this] {
        ELG_THREAD_NAME("Elegance network");
        io_.run();
        OPENSSL_thread_stop(); // this thread's OpenSSL state
    }) {
        // OpenSSL cleans up at exit, but not the state of threads still running then. Initializing
        // it here schedules that cleanup before this object's destruction, which ends the thread, so
        // it runs after it.
        OPENSSL_init_ssl(0, nullptr);
    }

    asio::io_context io_;
    ConnectionPool pool_; // closed after the thread stops, before the io_context goes
    asio::thread_pool blocking_{2};
    asio::executor_work_guard<asio::io_context::executor_type> work_;
    std::thread thread_;
};

// Runs `work` on a blocking thread and resumes with its result. On a strand of its own: when the
// awaiting coroutine is cancelled, co_spawn forwards that to the spawned one through the latter's
// executor, and on the bare pool that could run on its other thread while the spawned coroutine
// checks its cancellation state or completes.
template <class Work>
awaitable<std::invoke_result_t<Work&>> off_network_thread(Work work) {
    using Result = std::invoke_result_t<Work&>;
    co_return co_await asio::co_spawn(
        asio::make_strand(Network::instance().blocking()),
        [work = std::move(work)]() mutable -> awaitable<Result> { co_return work(); }, use_awaitable);
}

// The system's root certificates (net/net_backend.h): the certificate store on Windows, the
// keychain's anchors on macOS, the distribution's bundle elsewhere (OpenSSL's own default path is
// vcpkg's, not the system's). Returns how many it added.
int add_system_roots(ssl::context& context) {
    X509_STORE* store = SSL_CTX_get_cert_store(context.native_handle());
    const SystemRootCertificates roots = system_root_certificates();
    int added = 0;
    for (const std::vector<unsigned char>& der : roots.certificates) {
        const unsigned char* data = der.data();
        if (X509* certificate = d2i_X509(nullptr, &data, static_cast<long>(der.size()))) {
            added += X509_STORE_add_cert(store, certificate) == 1;
            X509_free(certificate);
        }
    }
    if (roots.bundles.empty()) {
        return added;
    }
    boost::system::error_code ignored;
    static_cast<void>(context.set_default_verify_paths(ignored)); // SSL_CERT_FILE and SSL_CERT_DIR, when set
    for (const std::filesystem::path& bundle : roots.bundles) {
        std::error_code missing;
        if (std::filesystem::exists(bundle, missing)) {
            static_cast<void>(context.load_verify_file(bundle.string(), ignored));
            if (!ignored) {
                added = sk_X509_OBJECT_num(X509_STORE_get0_objects(store));
                break;
            }
        }
    }
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

// One context per set of trusted roots, so kept connections can be matched by it. Network thread.
std::shared_ptr<ssl::context> context_for(const std::string& roots) {
    static std::map<std::string, std::shared_ptr<ssl::context>> contexts;
    std::shared_ptr<ssl::context>& context = contexts[roots];
    if (!context) {
        context = make_context(roots);
    }
    return context;
}

// The most a response's status line and header fields may take together.
constexpr std::size_t kMaxHeader = std::size_t{64} * 1024;

// "64 MiB", "16 KiB", "100 bytes".
std::string describe_size(std::size_t bytes) {
    constexpr std::size_t kKiB = 1024;
    if (bytes >= kKiB * kKiB && bytes % (kKiB * kKiB) == 0) {
        return std::format("{} MiB", bytes / (kKiB * kKiB));
    }
    if (bytes >= kKiB && bytes % kKiB == 0) {
        return std::format("{} KiB", bytes / kKiB);
    }
    return std::format("{} bytes", bytes);
}

// "30 s", "200 ms".
std::string describe_duration(std::chrono::milliseconds duration) {
    if (duration.count() % 1000 == 0) {
        return std::format("{} s", duration.count() / 1000);
    }
    return std::format("{} ms", duration.count());
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
    if (error == bhttp::error::header_limit) {
        return std::format("the response's header section is larger than {}", describe_size(kMaxHeader));
    }
    if (error == e::eof || error == e::connection_reset || error == e::connection_aborted ||
        error == bhttp::error::end_of_stream || error == bhttp::error::partial_message) {
        return "the connection was closed early";
    }
    if (error.category() == make_error_code(bhttp::error::bad_version).category()) { // Beast's parser
        return "the server's response isn't valid HTTP";
    }
    return error.message();
}

struct Validators {
    std::string etag;
    std::string last_modified;
};

// Our own failures, in words (a proxy's refusal).
struct FetchFailure : std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct Reply {
    unsigned status = 0;
    bool keep_alive = false;
    std::string location;
    CacheHeaders cache;
    std::vector<std::uint8_t> body;
};

// Where a URL's request goes.
struct Target {
    bool secure = false;
    std::string host;      // for connecting and the certificate check (no brackets)
    bool host_is_name = false;
    std::string port;
    std::string authority; // the Host header, and a CONNECT's target
    std::string path;      // "/a?b"
    std::string absolute;  // the whole URL, for plain http through a proxy
};

Target target_of(const urls::url& url) {
    Target target;
    target.secure = url.scheme_id() == urls::scheme::https;
    target.host = url.host_address();
    target.host_is_name = url.host_type() == urls::host_type::name;
    target.port = url.has_port() && !url.port().empty() ? std::string(url.port()) : target.secure ? "443" : "80";
    target.authority = std::format("{}:{}", std::string(url.encoded_host()), target.port);
    target.path = url.encoded_path().empty() ? "/" : std::string(url.encoded_path());
    if (url.has_query()) {
        target.path += '?';
        target.path += url.encoded_query();
    }
    target.absolute = std::format("http://{}{}", target.authority, target.path);
    return target;
}

// Every value of a header field that may be sent more than once, joined with ", " as RFC 9110
// §5.3 allows: `Cache-Control: max-age=600` and `Cache-Control: no-store` mean both.
template <class Fields>
std::string all_values(const Fields& fields, bhttp::field name) {
    std::string out;
    for (auto [it, end] = fields.equal_range(name); it != end; ++it) {
        if (!out.empty()) {
            out += ", ";
        }
        out += std::string_view(it->value());
    }
    return out;
}

// These coroutines take their streams, buffers and options by reference: each is co_awaited
// right away by its caller, whose frame owns them for as long as it runs.
// NOLINTBEGIN(cppcoreguidelines-avoid-reference-coroutine-parameters)
template <class Stream>
awaitable<Reply> exchange(Stream& stream, beast::flat_buffer& buffer, const Target& target,
                          const std::optional<Proxy>& proxy, const Validators* validators, std::size_t max_body) {
    const bool through_proxy = proxy && !target.secure; // https goes through a tunnel instead
    bhttp::request<bhttp::empty_body> request{bhttp::verb::get, through_proxy ? target.absolute : target.path, 11};
    request.set(bhttp::field::host, target.authority);
    request.set(bhttp::field::user_agent, "Elegance");
    request.set(bhttp::field::accept, "*/*");
    request.set(bhttp::field::accept_encoding, "identity");
    if (through_proxy && !proxy->authorization.empty()) {
        request.set(bhttp::field::proxy_authorization, proxy->authorization);
    }
    if (validators) {
        if (!validators->etag.empty()) {
            request.set(bhttp::field::if_none_match, validators->etag);
        }
        if (!validators->last_modified.empty()) {
            request.set(bhttp::field::if_modified_since, validators->last_modified);
        }
    }
    // Through a serializer: the overload that takes the message has a default argument GCC reports as
    // a zero null pointer at each call (-Wzero-as-null-pointer-constant).
    bhttp::request_serializer<bhttp::empty_body> serializer{request};
    co_await bhttp::async_write(stream, serializer, use_awaitable);

    bhttp::response_parser<bhttp::vector_body<std::uint8_t>> parser;
    parser.header_limit(kMaxHeader);
    parser.body_limit(max_body);
    co_await bhttp::async_read(stream, buffer, parser, use_awaitable);
    auto& response = parser.get();
    Reply reply;
    reply.status = response.result_int();
    // A body that ran to the end of the connection leaves nothing to reuse.
    reply.keep_alive = response.keep_alive() && !parser.need_eof();
    reply.location = std::string(response[bhttp::field::location]);
    reply.cache = {
        .cache_control = all_values(response, bhttp::field::cache_control),
        .expires = std::string(response[bhttp::field::expires]),
        .date = std::string(response[bhttp::field::date]),
        .age = std::string(response[bhttp::field::age]),
        .etag = std::string(response[bhttp::field::etag]),
        .last_modified = std::string(response[bhttp::field::last_modified]),
        .vary = all_values(response, bhttp::field::vary),
    };
    reply.body = std::move(response.body());
    co_return reply;
}

// Asks an HTTP proxy for a tunnel to the target (for TLS through it).
awaitable<void> tunnel(beast::tcp_stream& tcp, const Target& target, const Proxy& proxy) {
    bhttp::request<bhttp::empty_body> request{bhttp::verb::connect, target.authority, 11};
    request.set(bhttp::field::host, target.authority);
    request.set(bhttp::field::user_agent, "Elegance");
    if (!proxy.authorization.empty()) {
        request.set(bhttp::field::proxy_authorization, proxy.authorization);
    }
    bhttp::request_serializer<bhttp::empty_body> serializer{request}; // as in exchange()
    co_await bhttp::async_write(tcp, serializer, use_awaitable);
    beast::flat_buffer buffer;
    bhttp::response_parser<bhttp::empty_body> parser;
    parser.header_limit(16 * 1024);
    parser.skip(true); // a CONNECT's response has no body
    co_await bhttp::async_read(tcp, buffer, parser, use_awaitable);
    const unsigned status = parser.get().result_int();
    if (status == 407) {
        throw FetchFailure(proxy.authorization.empty() ? "the proxy asks for a password"
                                                       : "the proxy didn't accept its password");
    }
    if (status < 200 || status >= 300) {
        throw FetchFailure(std::format("the proxy refused the connection (HTTP {})", status));
    }
    if (buffer.size() != 0) { // the server can't have spoken before the TLS handshake
        throw FetchFailure("the proxy's response isn't valid HTTP");
    }
}

awaitable<std::unique_ptr<Connection>> connect(const Target& target, const std::optional<Proxy>& proxy,
                                               ssl::context& tls) {
    const auto executor = co_await asio::this_coro::executor;
    asio::ip::tcp::resolver resolver(executor);
    beast::tcp_stream tcp(executor);
    try {
        const auto endpoints =
            co_await resolver.async_resolve(proxy ? proxy->host : target.host, proxy ? proxy->port : target.port, use_awaitable);
        co_await tcp.async_connect(endpoints, use_awaitable);
    } catch (const boost::system::system_error& failure) {
        if (proxy && failure.code() != asio::error::operation_aborted) {
            throw FetchFailure(std::format("couldn't connect to the proxy: {}", describe(failure.code())));
        }
        throw;
    }
    if (!target.secure) {
        co_return std::make_unique<Connection>(std::move(tcp));
    }
    if (proxy) {
        co_await tunnel(tcp, target, *proxy);
    }
    auto connection = std::make_unique<Connection>(std::move(tcp), tls);
    auto& stream = std::get<1>(connection->stream);
    if (target.host_is_name) {
        // SNI. What SSL_set_tlsext_host_name() expands to, without its C cast (OpenSSL only reads the name).
        SSL_ctrl(stream.native_handle(), SSL_CTRL_SET_TLSEXT_HOSTNAME, TLSEXT_NAMETYPE_host_name,
                 const_cast<char*>(target.host.c_str()));
    }
    stream.set_verify_mode(ssl::verify_peer);
    stream.set_verify_callback(ssl::host_name_verification(target.host));
    co_await stream.async_handshake(ssl::stream_base::client, use_awaitable);
    co_return connection;
}

// A kept-open connection the server closed meanwhile fails like this on its next request. Windows
// reports it as aborted (WSAECONNABORTED) or not connected (WSAENOTCONN) too.
bool closed_while_idle(const boost::system::error_code& error) {
    return error == asio::error::eof || error == asio::error::connection_reset || error == asio::error::broken_pipe ||
           error == asio::error::connection_aborted || error == asio::error::not_connected ||
           error == bhttp::error::end_of_stream || error == ssl::error::stream_truncated;
}

// One request, no redirects, on a kept-open connection if there is one. Throws
// boost::system::system_error and FetchFailure.
awaitable<Reply> request(const urls::url& url, const FetchOptions& options, ssl::context& tls,
                         const Validators* validators) {
    const Target target = target_of(url);
    const std::string scheme = target.secure ? "https" : "http";
    ProxyChoice choice = options.proxy ? (options.proxy->empty() ? ProxyChoice(std::nullopt) : parse_proxy_url(*options.proxy))
                                       : co_await off_network_thread([scheme, host = target.host, port = target.port] {
                                             return find_proxy(scheme, host, port);
                                         });
    if (!choice) {
        throw FetchFailure(choice.error());
    }
    const std::optional<Proxy>& proxy = *choice;
    const std::string key = std::format("{}://{}:{} {}:{}:{} {}", scheme, target.host, target.port,
                                        proxy ? proxy->host : "", proxy ? proxy->port : "",
                                        proxy ? proxy->authorization : "", static_cast<const void*>(&tls));
    ConnectionPool& pool = Network::instance().pool();
    for (int attempt = 0;; ++attempt) {
        std::unique_ptr<Connection> connection = attempt == 0 ? pool.take(key) : nullptr;
        const bool reused = connection != nullptr;
        if (!connection) {
            connection = co_await connect(target, proxy, tls);
        }
        Reply reply;
        try {
            if (auto* plain = std::get_if<0>(&connection->stream)) {
                reply = co_await exchange(*plain, connection->buffer, target, proxy, validators, options.max_body);
            } else {
                reply = co_await exchange(std::get<1>(connection->stream), connection->buffer, target, proxy, validators,
                                          options.max_body);
            }
        } catch (const boost::system::system_error& failure) {
            if (reused && closed_while_idle(failure.code())) {
                continue; // once more, on a new connection (a GET can be repeated)
            }
            throw;
        }
        if (reply.keep_alive && connection->buffer.size() == 0) {
            pool.put(key, std::move(connection));
        }
        co_return reply;
    }
}

// One request, without following a redirect. Doesn't throw: the timeout's || would wait out its
// timer for an exception.
awaitable<std::expected<Reply, std::string>> attempt(const urls::url& url, const FetchOptions& options,
                                                     ssl::context& tls, const Validators* validators) {
    try {
        co_return co_await request(url, options, tls, validators);
    } catch (const FetchFailure& failure) {
        co_return std::unexpected(failure.what());
    } catch (const boost::system::system_error& failure) {
        if (failure.code() == bhttp::error::body_limit) {
            co_return std::unexpected(std::format("the response is larger than {}", describe_size(options.max_body)));
        }
        co_return std::unexpected(describe(failure.code()));
    }
}
// NOLINTEND(cppcoreguidelines-avoid-reference-coroutine-parameters)

// Where `location`, a redirect's Location from `from`, leads, or why it can't be followed.
std::expected<urls::url, std::string> redirect_target(const urls::url& from, std::string_view location) {
    const auto reference = urls::parse_uri_reference(location);
    urls::url next;
    if (!reference || !urls::resolve(from, *reference, next)) {
        return std::unexpected("the server redirected to an invalid URL");
    }
    if (from.scheme_id() == urls::scheme::https && next.scheme_id() != urls::scheme::https) {
        return std::unexpected("the server redirected from https to http");
    }
    return next;
}

// The request and its redirects, up to the final response. At each URL, a stored response answers
// while it's fresh (a stored redirect leads on from there), and is revalidated once it's stale; if
// the server can't be reached then, a stale one still answers unless it must be revalidated. Each
// response is stored under the URL that returned it.
awaitable<FetchResult> run(std::string text, FetchOptions options, std::shared_ptr<ssl::context> tls) {
    using namespace asio::experimental::awaitable_operators;
    const std::shared_ptr<HttpCache> cache = options.cache;
    const std::uint64_t generation = cache ? cache->generation() : 0;
    // In the background: the caller needn't wait.
    const auto keep = [cache, generation](CacheEntry entry) {
        asio::post(Network::instance().blocking(), [cache, generation, entry = std::move(entry)] {
            cache->store(entry, generation);
        });
    };
    const auto forget = [cache](std::string key) {
        asio::post(Network::instance().blocking(), [cache, key = std::move(key)] { cache->remove(key); });
    };
    const auto parsed = urls::parse_uri(text);
    if (!parsed) {
        co_return std::unexpected("not an http(s) URL");
    }
    urls::url url = *parsed;
    // One deadline for the whole request, redirects included.
    asio::steady_timer timer(co_await asio::this_coro::executor, options.timeout);
    for (int redirects = 0;; ++redirects) {
        if ((co_await asio::this_coro::cancellation_state).cancelled() != asio::cancellation_type::none) {
            co_return std::unexpected("cancelled");
        }
        if ((url.scheme_id() != urls::scheme::http && url.scheme_id() != urls::scheme::https) ||
            url.host_address().empty()) {
            co_return std::unexpected("not an http(s) URL");
        }
        const std::string key(url.buffer());
        std::optional<CacheEntry> cached;
        if (cache) {
            cached = co_await off_network_thread([cache, key] { return cache->find(key); });
        }
        std::string location; // where this URL redirects to
        if (cached && cached->fresh(unix_now())) {
            if (!cached->redirect()) {
                co_return std::move(cached->body);
            }
            location = cached->location;
        } else {
            const Validators validators = cached ? Validators{cached->headers.etag, cached->headers.last_modified}
                                                 : Validators{};
            const bool revalidating = cached && cached->revalidates();
            auto outcome = co_await (attempt(url, options, *tls, revalidating ? &validators : nullptr) ||
                                     timer.async_wait(use_awaitable));
            std::expected<Reply, std::string> result =
                outcome.index() == 1
                    ? std::unexpected(std::format("the request timed out (after {})", describe_duration(options.timeout)))
                    : std::move(std::get<0>(outcome));
            if (!result) {
                if (!cached || !cached->usable_stale()) {
                    co_return std::unexpected(std::move(result.error()));
                }
                // Offline: what we had, a stale redirect included.
                if (!cached->redirect()) {
                    co_return std::move(cached->body);
                }
                location = cached->location;
            } else if (Reply& reply = *result; reply.status == 304 && revalidating) {
                if (forbids_storing(reply.cache)) {
                    forget(key); // after it has answered
                } else {
                    refresh_cache_entry(*cached, reply.cache, unix_now());
                    keep(*cached);
                }
                if (!cached->redirect()) {
                    co_return std::move(cached->body);
                }
                location = cached->location;
            } else if (is_redirect_status(reply.status) && !reply.location.empty()) {
                if (cache) {
                    if (auto entry = make_cache_entry(key, reply.status, reply.location, reply.cache, {}, unix_now())) {
                        keep(std::move(*entry));
                    }
                }
                location = std::move(reply.location);
            } else if (reply.status < 200 || reply.status >= 300) {
                co_return std::unexpected(std::format("HTTP {}", reply.status));
            } else {
                if (cache && reply.status == 200) {
                    if (auto entry = make_cache_entry(key, reply.status, {}, reply.cache, reply.body, unix_now())) {
                        keep(std::move(*entry));
                    }
                }
                co_return std::move(reply.body);
            }
        }
        if (redirects == options.max_redirects) {
            co_return std::unexpected(std::format("too many redirects (more than {})", options.max_redirects));
        }
        std::expected<urls::url, std::string> next = redirect_target(url, location);
        if (!next) {
            co_return std::unexpected(std::move(next.error()));
        }
        url = std::move(*next);
    }
}

} // namespace

void FetchTask::cancel() const {
    if (!state_) {
        return;
    }
    State::Stage expected = State::Stage::pending;
    if (!state_->stage.compare_exchange_strong(expected, State::Stage::cancelled)) {
        return; // cancelled already, or its callback has started
    }
    asio::post(Network::instance().io(), [state = state_] { state->signal.emit(asio::cancellation_type::terminal); });
}

FetchTask fetch(std::string url, std::function<void(FetchResult)> done, FetchOptions options) {
    auto state = std::make_shared<FetchTask::State>();
    asio::io_context& io = Network::instance().io();
    // Every way the download ends goes through here, so its callback starts at most once, and
    // never once cancel() has returned.
    auto finish = [state, done = std::move(done), before = options.before_callback](FetchResult result) {
        if (before) {
            before();
        }
        if (state->start_callback()) {
            done(std::move(result));
        }
    };
    asio::post(io, [&io, state, url = std::move(url), finish = std::move(finish), options = std::move(options)]() mutable {
        if (state->stage != FetchTask::State::Stage::pending) {
            return; // cancelled before it began
        }
        std::shared_ptr<ssl::context> tls;
        try {
            tls = context_for(options.root_certificates);
        } catch (const boost::system::system_error& failure) {
            finish(std::unexpected(std::format("the secure connection failed: {}", failure.code().message())));
            return;
        }
        asio::co_spawn(io, run(std::move(url), std::move(options), std::move(tls)),
                       asio::bind_cancellation_slot(state->signal.slot(),
                                                    [finish = std::move(finish)](const std::exception_ptr& failure,
                                                                                 FetchResult result) {
                                                        if (failure) {
                                                            result = std::unexpected("the download failed");
                                                        }
                                                        finish(std::move(result));
                                                    }));
    });
    return FetchTask(state);
}

} // namespace elg::detail::net
