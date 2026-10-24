---
title: WebView
description: "Hosting the OS browser control as a view: the visual tree, input and focus, the script bridge, and the limits of native content."
sidebar:
  order: 3
depends: [controls/windows, controls/scrolling-and-lists, framework/input-and-focus]
---

:::caution[Design, not built]
Nothing here exists in the code yet. This is the proposed design, and it needs the decision on
the "no web content" non-goal first (see the checklist page).
:::

## What it's for

Some content is only available as a web page: OAuth sign-in, help pages, a map, an existing web
widget. A `WebView` hosts the **OS's browser control** (WebView2 on Windows, WKWebView on macOS,
WebKitGTK on Linux) inside an Elegance layout. Elegance doesn't render HTML; it positions,
clips and routes input to a control the OS draws.

## How it works

### API shape

The same split as video: a **handle** with state, and a **view** that shows it.

```cpp
const WebView web({.url = "https://example.com/login",
                   .on_navigate = [](const std::string& url) { return !url.starts_with("myapp://"); },
                   .on_message = [=](const std::string& json) { token = parse(json); }});
WebViewView(web).clip_shape(RoundedRectangle(12))
Text([=] { return web.title(); })
```

- Navigation: `load(url)`, `load_html`, `reload`, `stop`, `go_back`, `go_forward`.
- State (`State`s): `url`, `title`, `is_loading`, `progress`, `can_go_back`, `can_go_forward`,
  `error`.
- Script bridge: `evaluate(js, done)`, `post_message(json)` and `on_message`.
- Every callback runs on the UI thread.
- Copies share one browser.

### The control is asynchronous

WebView2 creation is a chain of asynchronous COM calls (environment → controller → webview),
taking a noticeable time the first time. The handle exists at once. Requests made before the
control is ready are **queued** and applied in order when it arrives, as `VideoPlayer` does with
its decoder. The view shows a placeholder until then.

The UI thread must be an **STA** COM apartment with a message pump, which SDL's loop provides.
WebView2's completion handlers then arrive on the UI thread, where they can write state directly.
The loader DLL is loaded **dynamically** on first use. When the WebView2 runtime is missing, the
handle's `error` says so and the app keeps running.

### Placement: visual hosting

Each Elegance window on Windows already presents through a DirectComposition visual tree (see
[Windows and DirectComposition](../../controls/windows/)). WebView2's **composition controller**
renders into a DComp visual it's given, so:

1. Create a child visual **above** the window's content visual, on the same DComp device, and set
   it as the controller's `RootVisualTarget`.
2. After each layout, the `WebViewElement` reports its **window frame**: points × scale for the
   visual's offset and the controller's `Bounds`.
3. **Clip** with an `IDCompositionRectangleClip` for the enclosing clips; it supports corner radii,
   so rounded clips work.
4. **Hide** the visual when the element is culled (scrolled out), when an ancestor fades to zero,
   or while a transition removes it.

### Keeping in step with frames

The visual's position must change in the **same composition commit** as the Vulkan frame that
moved the views around it. If the UI thread commits it on its own, the page lags a frame behind
while scrolling. The likely design: position updates travel in the frame packet, and the render
thread applies and commits them with the present. This needs measuring, because it decides where
the DComp calls happen.

### Input and focus

With visual hosting, the host owns input:

- **Pointer**: when hit testing lands on a `WebViewElement`, the dispatcher forwards mouse, wheel
  and pointer input through `SendMouseInput` / `SendPointerInput`, translated to the view's
  coordinates. The page's `CursorChanged` sets the cursor.
- **Focus**: the element joins the window's `FocusManager`. Gaining focus calls `MoveFocus`
  (programmatic, next or previous, matching how focus arrived). When the user tabs out of the
  page, WebView2 raises `MoveFocusRequested`, and Elegance moves focus to the next or previous
  element.
- **Keys** go to the page while it has focus. `AcceleratorKeyPressed` lets app shortcuts (F12, a
  quit shortcut) reach Elegance first.

### The script bridge

`post_message(json)` calls `PostWebMessageAsJson`; the page receives it on
`window.chrome.webview`. Messages from `window.chrome.webview.postMessage` arrive through
`WebMessageReceived` and are passed to `on_message` as JSON text. `evaluate(js, done)` calls
`ExecuteScript` and returns the result as JSON.

### Navigation policy

`NavigationStarting` asks `on_navigate(url)`; `false` cancels. That's how an OAuth flow catches
its redirect URL. `NewWindowRequested` opens the system browser by default instead of a new
WebView2 window.

## Rules

- The browser control is the OS's; nothing is bundled.
- Native content always draws **above** Elegance content in its window (see below); design every
  feature around that.
- Everything the app sees happens on the UI thread.

## Edge cases and limits

- **Airspace**: Elegance can't draw over the page: no overlays, in-window tooltips, shadows across
  it, `mask`, opacity groups or blur behind it. WebView2 has no API to render into a texture
  Elegance could composite. Anything that must cover the page needs a popup window (menus and
  popovers already are).
- **Transforms**: scale and rotation can't apply to the page; transitions should fade or hide it.
- **User data folder**: one per process; two instances sharing a folder fail to start. Default
  under `pref_path()`.
- **Accessibility**: WebView2 exposes its own UI Automation tree. Whether AccessKit can attach a
  foreign subtree under one of its nodes needs investigating; otherwise the view is one labeled
  element.
- **Other platforms**: WKWebView is an `NSView` above the Metal layer. WebKitGTK needs a GTK main
  loop next to SDL's, which needs investigating before it's promised.
