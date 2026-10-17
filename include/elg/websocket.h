// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_WEBSOCKET_H
#define ELG_WEBSOCKET_H

// WebSockets (design.md §13.3 networking follow-ups) on the system's own networking: WinHTTP on
// Windows. The connection runs on background threads; its events arrive on the UI thread, where
// state may be written.
//
//   const auto socket = http::WebSocket::connect({.url = "wss://echo.example.com"}, {
//       .on_open = [=] { status = "Connected"; },
//       .on_message = [=](const std::string& text) { log.update([&](auto& l) { l.push_back(text); }); },
//       .on_close = [=](int code, const std::string& reason) { status = reason; },
//   });
//   socket.send("hello");

#include <elg/export.h>
#include <elg/http.h>

#include <chrono>
#include <functional>
#include <memory>
#include <string>

namespace elg::http {

struct WebSocketRequest {
    std::string url; // ws:// or wss://
    Headers headers; // sent with the opening handshake
    std::chrono::milliseconds timeout{30'000}; // for the handshake only
};

// Each runs on the UI thread. After on_close nothing else runs.
struct WebSocketEvents {
    std::function<void()> on_open;
    // A whole message (text, or binary as bytes), however many frames it came in.
    std::function<void(const std::string&)> on_message;
    // Binary messages, when set (on_message then gets only text ones).
    std::function<void(const std::string&)> on_binary;
    // The server closed the connection (its close code and reason), or it failed: code 1006 and
    // why, in words (couldn't connect, the handshake was refused, the connection dropped).
    std::function<void(int code, const std::string& reason)> on_close;
};

// A WebSocket connection. Copies refer to the same connection; it's closed when the last copy
// goes. A default-constructed WebSocket is not connected, and its methods do nothing.
class ELG_API WebSocket {
public:
    WebSocket() = default;

    // Starts connecting; call from any thread.
    static WebSocket connect(WebSocketRequest request, WebSocketEvents events);

    // Sends a text message (UTF-8). Queued while connecting; dropped once closed. Any thread.
    void send(std::string text) const;
    // Sends a binary message, the same way.
    void send_binary(std::string bytes) const;
    // Closes the connection with this code and reason; its events don't run anymore. Any thread.
    void close(int code = 1000, std::string reason = {}) const;
    // Open: connected and not closed (yet). UI thread, as events see it.
    bool is_open() const;

    struct State;
    struct Owner;

private:
    static void close_state(State& state, int code, std::string reason);

    ELG_PRIVATE_MEMBERS_BEGIN
    std::shared_ptr<Owner> owner_;
    ELG_PRIVATE_MEMBERS_END
};

} // namespace elg::http

#endif // ELG_WEBSOCKET_H
