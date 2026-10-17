---
title: HTTP, AsyncImage, and WebSockets
description: "Requests on the OS networking stack, results on the UI thread, reactive fetches, remote images, and WebSocket connections."
sidebar:
  order: 1
depends: [graphics/images, framework/dynamic-content]
---

## What it's for

Desktop apps talk to servers: they fetch JSON, show remote avatars, and keep live connections.
Elegence does this on the **operating system's own networking** rather than bundling a library,
so apps get the system's certificate store, proxy configuration, HTTP/2 and compression for free,
and the binary stays small.

## How it works

### Requests

`http::fetch(Request, done)`:

1. A small pool of worker threads (up to 6) each runs one request at a time with a **blocking**
   WinHTTP call sequence: open a session (system proxy settings, automatic proxy where
   supported, HTTP/2 enabled), connect, send, receive headers, read the body (decompressing
   gzip/deflate), following redirects.
2. Errors become words: WinHTTP's error codes map to messages from `winhttp.dll` ("the operation
   timed out", "the server name could not be resolved", TLS failures, "cancelled").
3. The `Response` (status, headers with case-insensitive lookup, body) or the error is posted to
   the UI thread, where `done` runs and may write state.

`fetch` returns a `Task`. `cancel()` **closes the WinHTTP request handle**, which makes the
blocking call on the worker return at once with "cancelled". `done` then isn't called.

On Linux the same interface runs on libcurl when present; macOS (NSURLSession) is a follow-up.

### Reactive fetches

`http::Fetch(request)` wraps a request as state: `is_loading()`, `has_response()`, `ok()`,
`response()`, `error()` are `State`s, and `reload()` cancels and starts again. Copies share one
request, which is cancelled when the last copy goes. A view can show a spinner, then the data,
with ordinary reactive arguments.

### AsyncImage

`AsyncImage(url, {.scale, .content_mode})`, or `AsyncImage(url, content(phase), scale)` to choose
the view per phase (`AsyncImagePhase::Kind::loading`, `success` with `phase.image`, `failure` with
`phase.error`):

- The request starts **when the view is lowered**, so List rows that are never built never
  download. It's cancelled when the view goes (its scope ends).
- The body is checked with `probe_image` to be a decodable image before decoding.
- Downloads are kept in an in-memory **LRU cache by URL** (64 MB), so scrolling back doesn't
  fetch again.
- The phase view is the **single item of a ForEach keyed by the phase**. A phase change is an
  ordinary keyed replacement, so the spinner fades out as the image fades in.
- Failure shows the `image-broken` icon with the error as its tooltip.

### WebSockets

`http::WebSocket::connect({url, headers, timeout}, {on_open, on_message, on_binary, on_close})`:

- WebSockets need HTTP/1.1, so they use their own WinHTTP session (the shared one enables
  HTTP/2). `ws://` and `wss://` are rewritten to `http(s)://` for WinHTTP's URL parser.
- **Handshake**: an HTTP upgrade request with the headers, then `WinHttpWebSocketCompleteUpgrade`.
  A refused upgrade reports its status.
- **Two threads per connection**:
  - the **reader** connects, then loops on blocking receives with no timeout (messages can be far
    apart), reassembling fragments into whole messages;
  - the **writer** sends queued messages in order, and finally the close frame.
- **Events** go to the UI thread with `post_ui_task`, each checked against a "closed" flag, so
  nothing runs after `on_close` or after the app called `close()`. Binary messages go to
  `on_binary` when set, else to `on_message`.
- **Closing**: `close(code, reason)` queues the close frame. The reader gets the server's reply
  and stops. If a server never answers, closing the handle cancels the pending receive. Failures
  (couldn't connect, refused, dropped) arrive as `on_close(1006, reason)`.
- Copies share the connection, which closes when the last copy goes. `send` and `close` may be
  called from any thread; sends queued while connecting go out after the handshake.

## Rules

- Never block the UI thread on the network.
- Results and events only on the UI thread.
- Cancelling is immediate and final: no callback afterwards.

## Edge cases

- **Too many requests**: beyond the pool size they queue.
- **No disk cache yet**: `Cache-Control` and `ETag` are ignored; a disk cache is a follow-up.
- **Progress and streaming bodies** aren't exposed; responses arrive whole.
