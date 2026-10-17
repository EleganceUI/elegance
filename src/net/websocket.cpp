// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// WebSockets on the system's networking: WinHTTP on Windows. Each connection has a reader thread
// (connect, then blocking receives) and a writer thread (queued sends, then the close); events go
// to the UI thread.

#include <elg/websocket.h>

#include "core/ui_tasks.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <expected>
#include <format>
#include <mutex>
#include <optional>
#include <string_view>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#include <winhttp.h>
#endif

namespace elg::http {

struct WebSocket::State {
    WebSocketEvents events;
    std::atomic<bool> closed{false}; // close() was called, or on_close ran: no more events
    std::atomic<bool> open{false};   // as the UI thread's events see it

    std::mutex mutex;
    std::condition_variable ready; // the writer: something to send, or closing
    std::deque<std::pair<bool, std::string>> outgoing; // binary?, the message
    std::optional<std::pair<int, std::string>> close_request;
    bool connected = false; // the handshake finished (the writer may start)
    bool finished = false;  // the reader stopped: the writer stops too
#if defined(_WIN32)
    HINTERNET socket = nullptr;

    ~State() {
        if (socket) {
            WinHttpCloseHandle(socket);
        }
    }
#endif

    // Runs an event on the UI thread unless the connection was closed by then.
    template <class F>
    static void post(const std::shared_ptr<State>& state, F event) {
        detail::post_ui_task([state, event = std::move(event)] {
            if (!state->closed) {
                event(*state);
            }
        });
    }

    void post_close(const std::shared_ptr<State>& self, int code, std::string reason) {
        post(self, [code, reason = std::move(reason)](State& state) {
            state.closed = true;
            state.open = false;
            if (state.events.on_close) {
                state.events.on_close(code, reason);
            }
        });
    }
};

// Closes the connection when the last WebSocket copy goes.
struct WebSocket::Owner {
    std::shared_ptr<State> state;
    ~Owner() { WebSocket::close_state(*state, 1000, {}); }
};

namespace {

#if defined(_WIN32)

std::wstring widen(std::string_view text) {
    std::wstring wide(text.size(), L'\0');
    const int n = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(),
                                      static_cast<int>(wide.size()));
    wide.resize(static_cast<std::size_t>(std::max(n, 0)));
    return wide;
}

std::string describe(DWORD error) {
    switch (error) {
    case ERROR_WINHTTP_TIMEOUT: return "the connection timed out";
    case ERROR_WINHTTP_NAME_NOT_RESOLVED: return "the server's name couldn't be resolved";
    case ERROR_WINHTTP_CANNOT_CONNECT: return "couldn't connect to the server";
    case ERROR_WINHTTP_CONNECTION_ERROR: return "the connection was reset";
    case ERROR_WINHTTP_SECURE_FAILURE: return "the secure connection failed (certificate or TLS)";
    case ERROR_WINHTTP_OPERATION_CANCELLED: return "cancelled";
    case ERROR_WINHTTP_UNRECOGNIZED_SCHEME:
    case ERROR_WINHTTP_INVALID_URL: return "not a ws(s) URL";
    default: return std::format("network error {}", error);
    }
}

// WebSockets need HTTP/1.1, so they get their own session (the HTTP one enables HTTP/2).
HINTERNET session() {
    static const HINTERNET handle = [] {
        HINTERNET opened = WinHttpOpen(L"Elegence/0.1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                                       WINHTTP_NO_PROXY_BYPASS, 0);
        if (!opened) {
            opened = WinHttpOpen(L"Elegence/0.1", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                                 WINHTTP_NO_PROXY_BYPASS, 0);
        }
        return opened;
    }();
    return handle;
}

struct Handle {
    HINTERNET value = nullptr;
    ~Handle() {
        if (value) {
            WinHttpCloseHandle(value);
        }
    }
};

// The handshake: an HTTP/1.1 upgrade. Returns the socket, or why there isn't one.
std::expected<HINTERNET, std::string> open_socket(const WebSocketRequest& request) {
    HINTERNET http = session();
    if (!http) {
        return std::unexpected(describe(GetLastError()));
    }
    // WinHTTP cracks http(s) URLs only.
    std::string url = request.url;
    if (url.starts_with("wss://")) {
        url.replace(0, 3, "https");
    } else if (url.starts_with("ws://")) {
        url.replace(0, 2, "http");
    }
    const std::wstring wide_url = widen(url);
    URL_COMPONENTS parts{sizeof(URL_COMPONENTS)};
    parts.dwSchemeLength = parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(wide_url.c_str(), static_cast<DWORD>(wide_url.size()), 0, &parts)) {
        return std::unexpected(describe(GetLastError()));
    }
    const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
    std::wstring path(parts.lpszUrlPath, parts.dwUrlPathLength);
    path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    const bool secure = parts.nScheme == INTERNET_SCHEME_HTTPS;

    Handle connection{WinHttpConnect(http, host.c_str(), parts.nPort, 0)};
    if (!connection.value) {
        return std::unexpected(describe(GetLastError()));
    }
    Handle handle{WinHttpOpenRequest(connection.value, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                     WINHTTP_DEFAULT_ACCEPT_TYPES, secure ? WINHTTP_FLAG_SECURE : 0)};
    if (!handle.value) {
        return std::unexpected(describe(GetLastError()));
    }
    const int timeout = static_cast<int>(std::max<std::int64_t>(request.timeout.count(), 1));
    WinHttpSetTimeouts(handle.value, timeout, timeout, timeout, timeout);
    if (!WinHttpSetOption(handle.value, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0)) {
        return std::unexpected(describe(GetLastError()));
    }
    for (const auto& [name, value] : request.headers) {
        const std::wstring line = widen(name + ": " + value);
        WinHttpAddRequestHeaders(handle.value, line.c_str(), static_cast<DWORD>(line.size()),
                                 WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
    }
    if (!WinHttpSendRequest(handle.value, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(handle.value, nullptr)) {
        return std::unexpected(describe(GetLastError()));
    }
    DWORD status = 0;
    DWORD size = sizeof(status);
    WinHttpQueryHeaders(handle.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
    if (status != 101) {
        return std::unexpected(std::format("the server refused the WebSocket (HTTP {})", status));
    }
    // Messages can be far apart: receives wait as long as it takes.
    DWORD forever = 0;
    WinHttpSetOption(handle.value, WINHTTP_OPTION_RECEIVE_TIMEOUT, &forever, sizeof(forever));
    HINTERNET socket = WinHttpWebSocketCompleteUpgrade(handle.value, 0);
    if (!socket) {
        return std::unexpected(describe(GetLastError()));
    }
    return socket;
}

void write_loop(const std::shared_ptr<WebSocket::State>& state) {
    while (true) {
        std::unique_lock lock(state->mutex);
        state->ready.wait(lock, [&] {
            return state->finished || state->close_request || !state->outgoing.empty();
        });
        if (state->finished) {
            return;
        }
        if (!state->outgoing.empty()) {
            auto [binary, text] = std::move(state->outgoing.front());
            state->outgoing.pop_front();
            lock.unlock();
            WinHttpWebSocketSend(state->socket,
                                 binary ? WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE : WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,
                                 text.data(), static_cast<DWORD>(text.size()));
            continue; // a failed send shows up as a failed receive
        }
        const auto [code, reason] = *state->close_request;
        lock.unlock();
        // Sends the close frame; the reader gets the server's and stops.
        WinHttpWebSocketShutdown(state->socket, static_cast<USHORT>(code), const_cast<char*>(reason.data()),
                                 static_cast<DWORD>(std::min<std::size_t>(reason.size(), 123)));
        // A server that never answers would keep the reader waiting forever: closing the handle
        // cancels its receive.
        lock.lock();
        if (!state->ready.wait_for(lock, std::chrono::seconds(5), [&] { return state->finished; })) {
            WinHttpCloseHandle(state->socket);
            state->socket = nullptr;
        }
        return;
    }
}

void read_loop(const std::shared_ptr<WebSocket::State>& state, const WebSocketRequest& request) {
    auto socket = open_socket(request);
    if (!socket) {
        state->post_close(state, 1006, socket.error());
        return;
    }
    {
        std::lock_guard lock(state->mutex);
        state->socket = *socket;
        state->connected = true;
    }
    std::jthread writer([state] { write_loop(state); });
    WebSocket::State::post(state, [](WebSocket::State& s) {
        s.open = true;
        if (s.events.on_open) {
            s.events.on_open();
        }
    });

    std::string message;
    std::vector<char> buffer(64 * 1024);
    int code = 1006;
    std::string reason;
    while (true) {
        DWORD read = 0;
        WINHTTP_WEB_SOCKET_BUFFER_TYPE type{};
        const DWORD error = WinHttpWebSocketReceive(*socket, buffer.data(), static_cast<DWORD>(buffer.size()), &read, &type);
        if (error != NO_ERROR) {
            reason = state->closed ? std::string("closed") : describe(error);
            break;
        }
        if (type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) {
            USHORT status = 0;
            char text[123];
            DWORD length = 0;
            if (WinHttpWebSocketQueryCloseStatus(*socket, &status, text, sizeof(text), &length) == NO_ERROR) {
                code = status;
                reason.assign(text, length);
            }
            break;
        }
        message.append(buffer.data(), read);
        if (type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE || type == WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE) {
            const bool binary = type == WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE;
            WebSocket::State::post(state, [text = std::move(message), binary](WebSocket::State& s) {
                if (binary && s.events.on_binary) {
                    s.events.on_binary(text);
                } else if (s.events.on_message) {
                    s.events.on_message(text);
                }
            });
            message.clear();
        }
    }
    {
        std::lock_guard lock(state->mutex);
        state->finished = true;
    }
    state->ready.notify_all();
    state->post_close(state, code, std::move(reason));
}

#endif

} // namespace

WebSocket WebSocket::connect(WebSocketRequest request, WebSocketEvents events) {
    auto state = std::make_shared<State>();
    state->events = std::move(events);
    WebSocket socket;
    socket.owner_ = std::make_shared<Owner>(state);
#if defined(_WIN32)
    std::thread([state, request = std::move(request)] { read_loop(state, request); }).detach();
#else
    (void)request;
    state->post_close(state, 1006, "WebSockets aren't available in this build");
#endif
    return socket;
}

void WebSocket::send(std::string text) const {
    if (!owner_ || owner_->state->closed) {
        return;
    }
    State& state = *owner_->state;
    {
        std::lock_guard lock(state.mutex);
        state.outgoing.emplace_back(false, std::move(text));
    }
    state.ready.notify_all();
}

void WebSocket::send_binary(std::string bytes) const {
    if (!owner_ || owner_->state->closed) {
        return;
    }
    State& state = *owner_->state;
    {
        std::lock_guard lock(state.mutex);
        state.outgoing.emplace_back(true, std::move(bytes));
    }
    state.ready.notify_all();
}

void WebSocket::close(int code, std::string reason) const {
    if (owner_) {
        close_state(*owner_->state, code, std::move(reason));
    }
}

void WebSocket::close_state(State& state, int code, std::string reason) {
    if (state.closed.exchange(true)) {
        return;
    }
    state.open = false;
    {
        std::lock_guard lock(state.mutex);
        state.outgoing.clear();
        state.close_request.emplace(code, std::move(reason));
    }
    state.ready.notify_all();
}

bool WebSocket::is_open() const {
    return owner_ && owner_->state->open;
}

} // namespace elg::http
