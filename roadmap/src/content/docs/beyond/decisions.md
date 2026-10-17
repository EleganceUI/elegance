---
title: Open decisions
description: Questions the design still marks OPEN. Tick one when you've decided and written it down.
sidebar:
  order: 4
---

Each item is a decision, not code. Tick it once you've decided and recorded the answer in
`design.md`.

## Tasks

- [ ] Native-looking (OS-themed) widgets? Proposed: no; Elegence draws everything
- [ ] A `WebView` hosting the system browser control, as an exception to the "no web content" non-goal (see [WebView](../../system/webview/)), and whether a bundled-engine variant may ever exist as a separate package
- [ ] Bidi and line breaking library: SheenBidi, ICU4X, or libunibreak
- [ ] SVG library: lunasvg or resvg (or our own path renderer only)
- [ ] Global app state pattern: environment injection, explicit stores, or both
- [ ] Reactive node storage: arenas with generation indices vs `shared_ptr`
- [ ] camelCase aliases for SwiftUI muscle memory? Proposed: no
- [ ] Pipe syntax for modifiers (`Text("Hi") | card()`)? Proposed: no
- [ ] How far to take state preservation across hot reloads
- [ ] Crashpad for hard faults, or sentry-native if upload is wanted
- [ ] Which icon sets ship built in, and whether the extra Phosphor weights are opt-in
- [ ] A hinted small-size text fallback, if measurement calls for one
- [ ] Revisit C++26 once MSVC, GCC and Clang all ship reflection (`Store<T>`, inspector panels and theme binding can drop their macros)
