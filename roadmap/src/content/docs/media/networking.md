---
title: HTTP, AsyncImage, and WebSockets
description: Requests on the system's networking stack, reactive fetches, remote images, and WebSockets.
sidebar:
  order: 1
---

No bundled HTTP library. On Windows everything goes through WinHTTP, which brings the system's
certificates, proxies, HTTP/2 and compression.

:::note[Depends on]
`post_ui_task` from [core](../../foundations/core/) and [Raster images](../../graphics/images/) for `AsyncImage`.
:::

## Read first

- `design.md` §13.3 ("AsyncImage loads over HTTP(S)"), M5 "Media part 4", networking follow-ups
- `include/elg/http.h`, `src/net/http.cpp`, `src/view/async_image.cpp`, `tests/http_tests.cpp`
- `include/elg/websocket.h`, `src/net/websocket.cpp` (newer work)

## Tasks

### HTTP
- [ ] WinHTTP backend: system certificates and proxy settings (automatic proxy where supported), HTTP/2, gzip/deflate, redirects
- [ ] libcurl on Linux when present; NSURLSession on macOS
- [ ] Readable errors: timeout, name not resolved, TLS failure, cancelled, ...
- [ ] Case-insensitive header lookup
- [ ] `http::fetch(Request, done)`: one worker thread per request (up to 6); `done` runs on the UI thread; returns a `Task` whose `cancel()` closes the WinHTTP handle so a blocking call returns at once
- [ ] `http::Fetch`: a request as reactive state (`is_loading`, `has_response`, `ok`, `response`, `error`, `reload()`), cancelled with its last copy

### AsyncImage
- [ ] `AsyncImage(url, {.scale, .content_mode})` and `AsyncImage(url, content(phase))`
- [ ] Fetch when lowered (unbuilt List rows don't download); cancel when the view goes
- [ ] Check responses are images (`probe_image`)
- [ ] An in-memory cache by URL (64 MB LRU)
- [ ] The phase is the key of a one-item ForEach, so the spinner fades into the image; failure shows `image-broken` with the error as a tooltip

### WebSocket
- [ ] `http::WebSocket::connect({.url, .headers, .timeout}, {.on_open, .on_message, .on_binary, .on_close})` on WinHTTP's WebSocket API
- [ ] Events run on the UI thread; nothing runs after `on_close`
- [ ] Reassemble fragmented messages into whole text or binary messages
- [ ] `send` / `send_binary` from any thread, queued while connecting and dropped once closed; `close(code, reason)`
- [ ] Failures arrive as `on_close(1006, reason in words)`: couldn't connect, handshake refused, connection dropped
- [ ] Copies share one connection, which closes with the last copy

## Done when

- [ ] An image from a real HTTPS URL loads, and scrolling back to it doesn't fetch again
- [ ] Cancelling a slow request returns immediately
- [ ] A WebSocket echo test round-trips text and binary messages, and reports a refused handshake clearly
- [ ] Idle CPU stays 0 with requests finished

## Gotchas

- Not built yet: NSURLSession, a disk cache honoring `Cache-Control` / `ETag`, upload/download
  progress and streaming bodies, libcurl WebSockets elsewhere, and an optional JSON adapter
  (`elg/json.h`, `Response::json()`) behind a vcpkg feature.
